package main

import (
	"context"
	"errors"
	"testing"
	"time"

	"GryphCore/gen"
	"GryphCore/test_utils"
)

func TestBatchSpeedTestCancelledBeforeLookup(t *testing.T) {
	s := &server{}

	resp, err := s.Start(
		context.Background(),
		&gen.LoadConfigReq{
			CoreConfig: To(lifecycleTestConfig),
		},
	)

	if err != nil || resp == nil || resp.GetError() != "" {
		t.Fatalf("Start failed: response=%v err=%v", resp, err)
	}

	defer func() {
		_, _ = s.Stop(context.Background(), &gen.EmptyReq{})
	}()

	instance, _, release := acquireCurrentCoreTest()
	if instance == nil {
		t.Fatal("running core not found")
	}
	defer release()

	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	// These tags do not exist. Any outbound lookup would panic.
	tags := []string{"missing-one", "missing-two"}

	for _, countryOnly := range []bool{false, true} {
		if countryOnly {
			test_utils.CountryResults.Results()
		}

		results := test_utils.BatchSpeedTest(
			ctx,
			instance,
			tags,
			false,
			false,
			false,
			"",
			time.Second,
			countryOnly,
			1,
		)

		if len(results) != len(tags) {
			t.Fatalf(
				"expected %d results, got %d",
				len(tags), len(results),
			)
		}

		for idx, result := range results {
			if result.Tag != tags[idx] {
				t.Fatalf("incorrect result tag: %q", result.Tag)
			}

			if !result.Cancelled {
				t.Fatalf("result %q not cancelled", result.Tag)
			}

			if !errors.Is(result.Error, context.Canceled) {
				t.Fatalf(
					"expected context.Canceled, got %v",
					result.Error,
				)
			}
		}

		if countryOnly {
			reported := test_utils.CountryResults.Results()
			if len(reported) != len(tags) {
				t.Fatalf(
					"expected %d country reports, got %d",
					len(tags), len(reported),
				)
			}
		}
	}
}