package nativeaudio

import "testing"

// Closed-handle tests exercise the C ABI without opening a device or requesting capture.
func TestClosedHost(t *testing.T) {
	h := &Host{}
	h.Close()
	h.Close()
	if h.Start() == nil {
		t.Fatal("closed start accepted")
	}
	if h.Stop() == nil {
		t.Fatal("closed stop accepted")
	}
	if _, e := h.Devices(); e == nil {
		t.Fatal("closed devices accepted")
	}
	if _, e := h.Open("x", "", false, 48000, 10); e == nil {
		t.Fatal("closed open accepted")
	}
	if _, e := h.Snapshot(); e == nil {
		t.Fatal("closed snapshot accepted")
	}
}
