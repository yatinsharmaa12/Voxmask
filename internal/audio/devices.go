package audio

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"time"
	"voxmask.local/voxmask/native/audio"
)

type Device = nativeaudio.Device

func Devices() ([]Device, error) {
	h, err := nativeaudio.New()
	if err != nil {
		return nil, err
	}
	defer h.Close()
	return h.Devices()
}

type Options struct {
	Input    string
	Output   string
	Monitor  bool
	Rate     uint32
	PeriodMS uint32
	Duration time.Duration
}

func (o Options) Validate() error {
	if o.Input == "" {
		return errors.New("--input must be an explicit ID from devices")
	}
	if o.Monitor && o.Output == "" {
		return errors.New("monitor requires --output with an explicit headphone device ID")
	}
	if !o.Monitor && o.Output != "" {
		return errors.New("--output is only valid for monitor")
	}
	if o.Rate != 44100 && o.Rate != 48000 && o.Rate != 96000 {
		return errors.New("--rate must be 44100, 48000 or 96000")
	}
	if o.PeriodMS < 2 || o.PeriodMS > 40 {
		return errors.New("--period-ms must be between 2 and 40")
	}
	if o.Duration <= 0 || o.Duration > 30*time.Minute {
		return errors.New("--duration must be positive and at most 30m")
	}
	return nil
}

// Run polls metrics on the control plane. No audio callback or PCM enters Go.
func Run(ctx context.Context, o Options, w io.Writer) error {
	if err := o.Validate(); err != nil {
		return err
	}
	if err := ctx.Err(); err != nil {
		return err
	}
	h, err := nativeaudio.New()
	if err != nil {
		return err
	}
	defer h.Close()
	format, err := h.Open(o.Input, o.Output, o.Monitor, o.Rate, o.PeriodMS)
	if err != nil {
		return fmt.Errorf("initialize selected device (no default fallback): %w", err)
	}
	enc := json.NewEncoder(w)
	if err := enc.Encode(struct {
		Event           string `json:"event"`
		CallbackEngine  string `json:"callback_engine"`
		RequestedRate   uint32 `json:"requested_rate"`
		RequestedPeriod uint32 `json:"requested_period_ms"`
		RawMonitor      bool   `json:"raw_monitor"`
		nativeaudio.Format
	}{"configured", "native-cpp-worker", o.Rate, o.PeriodMS, o.Monitor, format}); err != nil {
		return err
	}
	if err := h.Start(); err != nil {
		return fmt.Errorf("start capture (check microphone permission): %w", err)
	}
	defer h.Stop()
	timer := time.NewTimer(o.Duration)
	defer timer.Stop()
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	var previous uint64
	var stalled int
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-timer.C:
			// Snapshot before intentional stop, which latches the native fault/silence gate.
			m, err := h.Snapshot()
			if err != nil {
				return err
			}
			if err := h.Stop(); err != nil {
				return err
			}
			if err := enc.Encode(m); err != nil {
				return err
			}
			if m.DeviceFault {
				return errors.New("audio pipeline fault (device interruption, invalid data or queue overflow); no fallback attempted")
			}
			if m.Callbacks == 0 {
				return errors.New("no capture callbacks; check device and microphone permission")
			}
			if m.InvalidBlocks > 0 {
				return errors.New("invalid or oversized audio blocks observed; inspect diagnostics")
			}
			return nil
		case <-ticker.C:
			m, err := h.Snapshot()
			if err != nil {
				return err
			}
			if err := enc.Encode(m); err != nil {
				return err
			}
			if m.DeviceFault {
				return errors.New("audio pipeline fault (device interruption, invalid data or queue overflow); no fallback attempted")
			}
			if m.Callbacks == previous {
				stalled++
			} else {
				stalled = 0
			}
			previous = m.Callbacks
			if stalled >= 3 {
				return errors.New("capture callback stalled for three seconds; stopping")
			}
			if m.InvalidBlocks > 0 {
				return errors.New("invalid or oversized audio blocks; stopping")
			}
		}
	}
}
