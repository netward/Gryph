package main

import (
	"context"
	"errors"
	"fmt"
	"testing"
	"time"

	"GryphCore/test_utils"
)

// Performs one URL test through an existing runtime.
// Same two-request behavior as temporary server.Test().
func pr45DRunURLBatch(
	rt *temporaryTestRuntime,
	url string,
) error {
	results := test_utils.BatchURLTest(
		context.Background(),
		rt.box,
		[]string{"direct"},
		url,
		1,
		true,
		3*time.Second,
	)

	if len(results) != 1 || results[0] == nil {
		return fmt.Errorf(
			"invalid URL test results: %v",
			results,
		)
	}

	return results[0].Error
}

// Runs a group of URL tests.
//
// reuse=false: one runtime for every URL test.
// reuse=true:  one runtime for the entire group.
//
// Runtime creation and shutdown are both measured.
func pr45DRunGroup(
	url string,
	count int,
	reuse bool,
) (err error) {
	if reuse {
		rt, createErr := newTemporaryTestRuntime(
			lifecycleTestConfig,
			false,
			"",
		)
		if createErr != nil {
			return createErr
		}

		defer func() {
			err = errors.Join(err, rt.Close())
		}()

		for i := 0; i < count; i++ {
			if testErr := pr45DRunURLBatch(
				rt, url,
			); testErr != nil {
				return testErr
			}
		}

		return nil
	}

	for i := 0; i < count; i++ {
		rt, createErr := newTemporaryTestRuntime(
			lifecycleTestConfig,
			false,
			"",
		)
		if createErr != nil {
			return createErr
		}

		testErr := pr45DRunURLBatch(rt, url)
		closeErr := rt.Close()

		if combined := errors.Join(
			testErr, closeErr,
		); combined != nil {
			return combined
		}
	}

	return nil
}

func BenchmarkPR45DRuntimeReuse(b *testing.B) {
	srv := newPR4BenchmarkServer(b)

	groupSizes := []int{1, 10, 50, 100}

	for _, count := range groupSizes {
		b.Run(
			fmt.Sprintf("Tests%d", count),
			func(b *testing.B) {
				cases := []struct {
					name  string
					reuse bool
				}{
					{"NewEach", false},
					{"ReuseWithinGroup", true},
				}

				for _, tc := range cases {
					b.Run(tc.name, func(b *testing.B) {
						b.ReportAllocs()
						b.ReportMetric(
							float64(count),
							"tests/op",
						)

						b.ResetTimer()

						for i := 0; i < b.N; i++ {
							err := pr45DRunGroup(
								srv.URL,
								count,
								tc.reuse,
							)
							if err != nil {
								b.Fatal(err)
							}

							// Reporter cleanup is excluded
							// from both variants.
							b.StopTimer()
							_ = test_utils.URLReporter.Results()
							b.StartTimer()
						}
					})
				}
			},
		)
	}
}
