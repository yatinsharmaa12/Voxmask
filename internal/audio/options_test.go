package audio

import (
	"testing"
	"time"
)

func TestOptions(t *testing.T) {
	base := Options{Input: "id", Rate: 48000, PeriodMS: 10, Duration: time.Second}
	if err := base.Validate(); err != nil {
		t.Fatal(err)
	}
	for _, edit := range []func(*Options){func(o *Options) { o.Input = "" }, func(o *Options) { o.Monitor = true }, func(o *Options) { o.Rate = 1 }, func(o *Options) { o.Duration = 0 }, func(o *Options) { o.PeriodMS = 0 }} {
		o := base
		edit(&o)
		if o.Validate() == nil {
			t.Fatal("invalid options accepted")
		}
	}
}
