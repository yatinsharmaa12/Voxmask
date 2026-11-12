// voxmask-audio is a development-only M1 harness. It does not transform voices.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"time"

	"voxmask.local/voxmask/internal/audio"
	nativeaudio "voxmask.local/voxmask/native/audio"
)

const usage = `VoxMask M1 audio harness (no AI or virtual microphone)
  voxmask-audio buffer-check
  voxmask-audio devices
  voxmask-audio probe --input ID [--duration 10s] [--rate 48000]
  voxmask-audio monitor --input ID --output ID --allow-raw-monitor

probe captures only numeric levels; monitor plays your ORIGINAL voice locally.
Use headphones. No recordings are saved. No capture occurs without a command.
`

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}
func run(args []string) error {
	if len(args) == 0 || args[0] == "help" || args[0] == "--help" {
		fmt.Print(usage)
		return nil
	}
	switch args[0] {
	case "buffer-check":
		if len(args) != 1 {
			return errors.New("buffer-check accepts no arguments")
		}
		report, err := nativeaudio.CheckBuffers()
		if err != nil {
			return err
		}
		return json.NewEncoder(os.Stdout).Encode(report)
	case "devices":
		if len(args) != 1 {
			return errors.New("devices accepts no arguments")
		}
		ds, err := audio.Devices()
		if err != nil {
			return err
		}
		return json.NewEncoder(os.Stdout).Encode(ds)
	case "probe", "monitor":
		fs := flag.NewFlagSet(args[0], flag.ContinueOnError)
		input := fs.String("input", "", "explicit capture device ID")
		output := fs.String("output", "", "explicit headphone device ID")
		duration := fs.Duration("duration", 10*time.Second, "capture duration (maximum 30m)")
		rate := fs.Uint("rate", 48000, "client sample rate: 44100, 48000 or 96000")
		period := fs.Uint("period-ms", 10, "requested callback period in milliseconds")
		allow := fs.Bool("allow-raw-monitor", false, "acknowledge original-voice local monitoring")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if fs.NArg() != 0 {
			return errors.New("unexpected positional arguments")
		}
		monitor := args[0] == "monitor"
		if monitor && !*allow {
			return errors.New("monitor plays ORIGINAL audio; use headphones and --allow-raw-monitor")
		}
		if !monitor && *allow {
			return errors.New("--allow-raw-monitor is only valid for monitor")
		}
		if *rate > 96000 || *period > 40 {
			return errors.New("rate or period out of range")
		}
		opts := audio.Options{Input: *input, Output: *output, Monitor: monitor, Rate: uint32(*rate), PeriodMS: uint32(*period), Duration: *duration}
		if err := opts.Validate(); err != nil {
			return err
		}
		if monitor {
			fmt.Fprintln(os.Stderr, "RAW MONITOR: original microphone audio to selected output; use headphones.")
		}
		ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
		defer stop()
		err := audio.Run(ctx, opts, os.Stdout)
		if errors.Is(err, context.Canceled) {
			return nil
		}
		return err
	default:
		return fmt.Errorf("unknown command %q; use --help", args[0])
	}
}
