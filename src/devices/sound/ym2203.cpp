// license:BSD-3-Clause
// copyright-holders:Aaron Giles

#include "emu.h"
#include "ym2203.h"


DEFINE_DEVICE_TYPE(YM2203, ym2203_device, "ym2203", "YM2203 OPN")


//*********************************************************
//  INLINE HELPERS
//*********************************************************

//-------------------------------------------------
//  linear_to_fp - given a 32-bit signed input
//  value, convert it to a signed 10.3 floating-
//  point value
//-------------------------------------------------

inline s16 linear_to_fp(s32 value)
{
	// start with the absolute value
	s32 avalue = (value < 0) ? -value : value;
	u8 shift = 0;

	// shift until absolute value fits in 9 bits (bit 10 is the sign)
	while (avalue != (avalue & 0x1ff))
	{
		avalue >>= 1;
		shift++;
	}

	// if out of range, just return maximum; note that YM3012 DAC does
	// not support a shift count of 7, so we clamp at 6
	if (shift >= 7)
		shift = 6, avalue = 0x1ff;

	// encode with shift in low 3 bits and signed mantissa in upper
	return shift | (((value < 0) ? -avalue : avalue) << 3);
}


//-------------------------------------------------
//  fp_to_linear - given a 10.3 floating-point
//  value, convert it to a signed 16-bit value,
//  clamping
//-------------------------------------------------

inline stream_buffer::sample_t fp_to_linear(s16 value)
{
	s32 result = (value >> 3) << BIT(value, 0, 3);
	if (result < -32768)
		result = -32768;
	else if (result > 32767)
		result = 32767;
	return stream_buffer::sample_t(result) * (1.0 / 32768.0);
}



//*********************************************************
//  YM2203 DEVICE
//*********************************************************

//-------------------------------------------------
//  ym2203_device - constructor
//-------------------------------------------------

ym2203_device::ym2203_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	ay8910_device(mconfig, YM2203, tag, owner, clock, PSG_TYPE_YM, 3, 2),
	m_opn(*this, false),
	m_stream(nullptr),
	m_address(0)
{
}


//-------------------------------------------------
//  read - handle a read from the device
//-------------------------------------------------

u8 ym2203_device::read(offs_t offset)
{
	u8 result = 0;
	switch (offset & 1)
	{
		case 0:	// status port
			result = m_opn.status();
			break;

		case 1: // data port (only SSG)
			if (m_address < 0x10)
				result = ay8910_read_ym();
			break;
	}
	return result;
}


//-------------------------------------------------
//  write - handle a write to the register
//  interface
//-------------------------------------------------

void ym2203_device::write(offs_t offset, u8 value)
{
	switch (offset & 1)
	{
		case 0:	// address port
			m_address = value;

			// write register to SSG
			if (m_address < 0x10)
				ay8910_write_ym(0, m_address);

			// prescaler select : 2d,2e,2f
			else if (m_address == 0x2d)
				update_prescale(6);
			else if (m_address == 0x2e && m_opn.clock_prescale() == 6)
				update_prescale(3);
			else if (m_address == 0x2f)
				update_prescale(2);
			break;

		case 1: // data port

			// write to SSG
			if (m_address < 0x10)
				ay8910_write_ym(1, value);

			// write to OPN
			else
			{
				m_stream->update();
				m_opn.write(m_address, value);
			}

			// mark busy for a bit
			m_opn.set_busy();
			break;
	}
}


//-------------------------------------------------
//  device_start - start of emulation
//-------------------------------------------------

void ym2203_device::device_start()
{
	// start the SSG device
	ay8910_device::device_start();

	// create our stream
	m_stream = stream_alloc(0, 1, clock() / (4 * 3 * 6));

	// save our data
	save_item(YMOPN_NAME(m_address));

	// save the OPN engine
	m_opn.save(*this);
}


//-------------------------------------------------
//  device_reset - start of emulation
//-------------------------------------------------

void ym2203_device::device_reset()
{
	// reset the SSG device
	ay8910_device::device_reset();

	// reset the OPN engine
	m_opn.reset();
}


//-------------------------------------------------
//  device_clock_changed - update if clock changes
//-------------------------------------------------

void ym2203_device::device_clock_changed()
{
	// refresh via prescale
	update_prescale(m_opn.clock_prescale());
}


//-------------------------------------------------
//  sound_stream_update - update the sound stream
//-------------------------------------------------

void ym2203_device::sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs)
{
	// if this is not our stream, pass it on
	if (&stream != m_stream)
	{
		ay8910_device::sound_stream_update(stream, inputs, outputs);
		return;
	}

	// force configured registers that are not relevant on the OPN
	m_opn.write(0x22, 0x00);	// disable LFO
	m_opn.write(0xb4, 0x80);	// pan left, disable LFO
	m_opn.write(0xb5, 0x80);	// pan left, disable LFO
	m_opn.write(0xb6, 0x80);	// pan left, disable LFO

	// iterate over all target samples
	for (int sampindex = 0; sampindex < outputs[0].samples(); sampindex++)
	{
		// clock the system
		m_opn.clock(0x07);

		// update the OPN content; OPN is full 14-bit with no intermediate clipping
		s32 lsum = 0, rsum = 0;
		m_opn.output(lsum, rsum, 0, 32767, 0x07);

		// convert to 10.3 floating point value for the DAC and back
		// OPN is mono, so only the left sum matters
		outputs[0].put(sampindex, fp_to_linear(linear_to_fp(lsum)));
	}
}


//-------------------------------------------------
//  update_prescale - set a new prescale value and
//  update clocks as needed
//-------------------------------------------------

void ym2203_device::update_prescale(u8 newval)
{
	// inform the OPN engine and refresh our clock rate
	m_opn.set_clock_prescale(newval);
	m_stream->set_sample_rate(clock() / (4 * 3 * newval));
	printf("Prescale = %d; sample_rate = %d\n", newval, clock() / (4 * 3 * newval));

	// also scale the SSG streams
	// mapping is (OPN->SSG): 6->4, 3->2, 2->1
	u8 ssg_scale = 2 * newval / 3;
	// QUESTION: where does the *2 come from??
	ay_set_clock(clock() * 2 / ssg_scale);
}
