package main

import "testing"

func TestRejectBeforeCapture(t *testing.T) {
	for _, args := range [][]string{{"bogus"}, {"monitor", "--input", "x", "--output", "y"}, {"probe"}, {"probe", "--input", "x", "--duration", "0s"}, {"probe", "--input", "x", "--rate", "4295015296"}, {"devices", "extra"}} {
		if run(args) == nil {
			t.Fatalf("accepted %v", args)
		}
	}
}
