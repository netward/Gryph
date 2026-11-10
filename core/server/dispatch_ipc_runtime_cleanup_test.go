package main

import (
	"encoding/binary"
	"errors"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"
	"time"

	"GryphCore/gen"
	"GryphCore/test_utils"

	"google.golang.org/protobuf/proto"
)

// Sends an IPC request containing a protobuf payload.
func writeIPCPayloadRequest(
	conn net.Conn,
	id uint32,
	method string,
	payload []byte,
) error {
	frame := make(
		[]byte,
		10+len(method)+len(payload),
	)

	binary.LittleEndian.PutUint32(frame[0:4], id)
	binary.LittleEndian.PutUint16(
		frame[4:6],
		uint16(len(method)),
	)

	copy(frame[6:], method)

	offset := 6 + len(method)

	binary.LittleEndian.PutUint32(
		frame[offset:offset+4],
		uint32(len(payload)),
	)

	copy(frame[offset+4:], payload)

	return writeIPCAll(conn, frame)
}

func TestIPCDisconnectReleasesTemporaryRuntime(
	t *testing.T,
) {
	tests := []struct {
		name    string
		method  string
		flush   bool
		payload func(string) ([]byte, error)
	}{
		{
			name:   "URLTest",
			method: "Test",
			payload: func(url string) ([]byte, error) {
				return proto.Marshal(&gen.TestReq{
					Config:             To(lifecycleTestConfig),
					TestCurrent:        To(false),
					UseDefaultOutbound: To(true),
					Url:                To(url),
					MaxConcurrency:     To(int32(1)),
					TestTimeoutMs:      To(int32(30000)),
				})
			},
		},
		{
			name:   "SpeedTest",
			method: "SpeedTest",
			flush:  true,
			payload: func(url string) ([]byte, error) {
				return proto.Marshal(&gen.SpeedTestRequest{
					Config:             To(lifecycleTestConfig),
					TestCurrent:        To(false),
					UseDefaultOutbound: To(true),
					TestDownload:       To(true),
					SimpleDownload:     To(true),
					SimpleDownloadAddr: To(url),
					TimeoutMs:          To(int32(30000)),
				})
			},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			test_utils.ResetTestContext()
			t.Cleanup(test_utils.ResetTestContext)

			requestStarted := make(chan struct{})
			requestFinished := make(chan struct{})

			var startOnce sync.Once
			var finishOnce sync.Once

			httpServer := httptest.NewServer(
				http.HandlerFunc(
					func(w http.ResponseWriter, r *http.Request) {
						if tc.flush {
							w.WriteHeader(http.StatusOK)
							_, _ = w.Write(
								[]byte("initial data"),
							)
							w.(http.Flusher).Flush()
						}

						startOnce.Do(func() {
							close(requestStarted)
						})

						// Keep the request active until
						// the client cancels it.
						<-r.Context().Done()

						finishOnce.Do(func() {
							close(requestFinished)
						})
					},
				),
			)

			client, server := net.Pipe()

			t.Cleanup(func() {
				_ = client.Close()
				httpServer.CloseClientConnections()
				httpServer.Close()
			})

			serveDone := make(chan error, 1)

			go func() {
				serveDone <- serveIPCWithContext(
					server,
					dispatchWithContext,
				)
			}()

			if err := client.SetWriteDeadline(
				time.Now().Add(5 * time.Second),
			); err != nil {
				t.Fatal(err)
			}

			payload, err := tc.payload(httpServer.URL)
			if err != nil {
				t.Fatal(err)
			}

			if err := writeIPCPayloadRequest(
				client,
				1,
				tc.method,
				payload,
			); err != nil {
				t.Fatalf("send IPC request: %v", err)
			}

			// The real sing-box runtime must have
			// reached the local HTTP endpoint.
			select {
			case <-requestStarted:

			case <-time.After(8 * time.Second):
				t.Fatal(
					"temporary test did not reach HTTP server",
				)
			}

			// Simulate unexpected GUI disconnection.
			if err := client.Close(); err != nil {
				t.Fatal(err)
			}

			// IPC must wait for the test handler
			// to finish its cleanup.
			select {
			case err := <-serveDone:
				if !errors.Is(err, io.EOF) {
					t.Fatalf(
						"unexpected IPC exit: %v",
						err,
					)
				}

			case <-time.After(8 * time.Second):
				t.Fatal(
					"IPC did not finish after disconnect",
				)
			}

			// The HTTP handler must observe
			// termination of its client request.
			select {
			case <-requestFinished:

			case <-time.After(3 * time.Second):
				t.Fatal(
					"HTTP request was not cancelled",
				)
			}

			// A released temporary runtime must
			// no longer appear in the tracker.
			temporaryTests.mu.Lock()
			active := len(temporaryTests.active)
			temporaryTests.mu.Unlock()

			if active != 0 {
				t.Fatalf(
					"%d temporary operations remain active",
					active,
				)
			}
		})
	}
}
