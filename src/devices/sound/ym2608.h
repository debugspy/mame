// license:BSD-3-Clause
// copyright-holders:Aaron Giles

#ifndef MAME_SOUND_YM2608_H
#define MAME_SOUND_YM2608_H

#pragma once

#include "ymopn.h"
#include "ymadpcm.h"


// ======================> ym2608_device

DECLARE_DEVICE_TYPE(YM2608, ym2608_device);

class ym2608_device : public ay8910_device, public device_rom_interface<21>
{
	enum : u8
	{
		STATUS_ADPCM_B_EOS = 0x04,
		STATUS_ADPCM_B_BRDY = 0x08,
		STATUS_ADPCM_B_ZERO = 0x10,
		STATUS_ADPCM_B_PLAYING = 0x20
	};

public:
	// constructor
	ym2608_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// read/write access
	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);

	// configuration helpers
	auto irq_handler() { return m_opn.irq_handler(); }

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_clock_changed() override;

	// ROM device overrides
	virtual const tiny_rom_entry *device_rom_region() const override;
	virtual void rom_bank_updated() override;

	// sound overrides
	virtual void sound_stream_update(sound_stream &stream, std::vector<read_stream_view> const &inputs, std::vector<write_stream_view> &outputs) override;

private:
	// set a new prescale value and update clocks
	void update_prescale(u8 newval);

	// combine ADPCM and OPN statuses
	u8 combine_status();

	// ADPCM read/write callbacks
	u8 adpcm_a_read(offs_t address);
	u8 adpcm_b_read(offs_t address);
	void adpcm_b_write(offs_t address, u8 data);

	// internal state
	required_memory_region m_internal; // internal memory region
	ymopn_engine m_opn;              // core OPN engine
	ymadpcm_a_engine m_adpcm_a;      // ADPCM-A engine
	ymadpcm_b_engine m_adpcm_b;      // ADPCM-B engine
	sound_stream *m_stream;          // sound stream
	u16 m_address;                   // address register
	u8 m_irq_enable;                 // IRQ enable register
	u8 m_flag_control;               // flag control register
};

#endif // MAME_SOUND_YM2608_H
