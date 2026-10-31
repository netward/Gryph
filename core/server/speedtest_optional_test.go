package main

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"

	"GryphCore/gen"
	"GryphCore/test_utils"
)

func TestSpeedTestOptionalDefaults(t *testing.T) {
	s := &server{}
	test_utils.ResetTestContext()

	t.Cleanup(func() {
		_, _ = s.Stop(context.Background(), &gen.EmptyReq{})
		test_utils.ResetTestContext()
	})

	start, err := s.Start(
		context.Background(),
		&gen.LoadConfigReq{
			CoreConfig: To(lifecycleTestConfig),
		},
	)
	if err != nil || start == nil || start.GetError() != "" {
		t.Fatalf("Start failed: %v, %v", start, err)
	}

	httpServer := httptest.NewServer(
		http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			_, _ = w.Write([]byte("test payload"))
		}),
	)
	defer httpServer.Close()

	// Deliberately omit most optional fields.
	resp, err := s.SpeedTest(
		context.Background(),
		&gen.SpeedTestRequest{
			TestCurrent:        To(true),
			SimpleDownload:     To(true),
			SimpleDownloadAddr: To(httpServer.URL),
		},
	)

	if err != nil {
		t.Fatal(err)
	}
	if resp == nil || len(resp.GetResults()) != 1 {
		t.Fatalf("unexpected response: %v", resp)
	}
	if got := resp.GetResults()[0].GetError(); got != "" {
		t.Fatalf("download failed: %s", got)
	}
}
