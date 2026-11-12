package nativeaudio

import "testing"

func TestBufferCheck(t *testing.T) {
	r, err := CheckBuffers()
	if err != nil {
		t.Fatal(err)
	}
	if !r.Passed || r.SimulatedFrames != 1440000 || r.OverflowBlocks != 1 || r.StaleFrames != 8160 || r.MissingFrames != 480 {
		t.Fatalf("unexpected simulation: %+v", r)
	}
}
