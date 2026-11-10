package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"strings"
	"sync"
	"testing"
	"time"
)

// Write a request in the real Gryph IPC wire format.
func writeIPCStressRequest(
	w io.Writer,
	id uint32,
	method string,
) error {
	var frame bytes.Buffer

	if err := binary.Write(
		&frame,
		binary.LittleEndian,
		id,
	); err != nil {
		return err
	}

	if err := binary.Write(
		&frame,
		binary.LittleEndian,
		uint16(len(method)),
	); err != nil {
		return err
	}

	if _, err := frame.WriteString(method); err != nil {
		return err
	}

	// Empty protobuf payload.
	if err := binary.Write(
		&frame,
		binary.LittleEndian,
		uint32(0),
	); err != nil {
		return err
	}

	return writeIPCAll(w, frame.Bytes())
}

// Read a complete Gryph IPC response frame.
func readIPCStressResponse(
	r io.Reader,
) (uint32, byte, []byte, error) {
	var header [9]byte

	if _, err := io.ReadFull(r, header[:]); err != nil {
		return 0, 0, nil, err
	}

	id := binary.LittleEndian.Uint32(header[:4])
	status := header[4]
	length := binary.LittleEndian.Uint32(header[5:])

	if length > maxRPCPayloadBytes {
		return 0, 0, nil, fmt.Errorf(
			"response too large: %d",
			length,
		)
	}

	data := make([]byte, int(length))

	if _, err := io.ReadFull(r, data); err != nil {
		return 0, 0, nil, err
	}

	return id, status, data, nil
}

func TestIPCStopTestUnderRegularSaturation(
	t *testing.T,
) {
	client, server := net.Pipe()
	defer client.Close()

	if err := client.SetDeadline(
		time.Now().Add(10 * time.Second),
	); err != nil {
		t.Fatal(err)
	}

	started := make(
		chan struct{},
		maxRegularRPC,
	)

	releaseRegular := make(chan struct{})

	var releaseOnce sync.Once

	releaseAll := func() {
		releaseOnce.Do(func() {
			close(releaseRegular)
		})
	}

	defer releaseAll()

	done := make(chan error, 1)

	go func() {
		done <- serveIPC(
			server,
			func(
				method string,
				_ []byte,
			) ([]byte, error) {
				switch method {

				case "Test":
					started <- struct{}{}

					// Simulate a long-running RPC.
					<-releaseRegular

					return []byte("regular-ok"), nil

				case "QueryStats":
					return []byte("telemetry-ok"), nil

				case "StopTest":
					// Simulate cancellation of the
					// long-running regular operations.
					releaseAll()

					return []byte("control-ok"), nil

				default:
					return nil, fmt.Errorf(
						"unexpected method %q",
						method,
					)
				}
			},
		)
	}()

	// Fill all 16 regular slots.
	for i := 1; i <= maxRegularRPC; i++ {
		if err := writeIPCStressRequest(
			client,
			uint32(i),
			"Test",
		); err != nil {
			t.Fatal(err)
		}

		// Ensure this handler is actually running.
		select {
		case <-started:

		case <-time.After(5 * time.Second):
			t.Fatalf(
				"regular handler %d did not start",
				i,
			)
		}
	}

	// The 17th regular request must be rejected.
	if err := writeIPCStressRequest(
		client,
		17,
		"Test",
	); err != nil {
		t.Fatal(err)
	}

	id, status, body, err :=
		readIPCStressResponse(client)

	if err != nil {
		t.Fatal(err)
	}

	if id != 17 ||
		status != 1 ||
		!strings.Contains(string(body), "RPC_BUSY") {
		t.Fatalf(
			"expected RPC_BUSY: id=%d status=%d body=%q",
			id,
			status,
			body,
		)
	}

	// Telemetry must remain responsive.
	if err := writeIPCStressRequest(
		client,
		18,
		"QueryStats",
	); err != nil {
		t.Fatal(err)
	}

	id, status, body, err =
		readIPCStressResponse(client)

	if err != nil {
		t.Fatal(err)
	}

	if id != 18 ||
		status != 0 ||
		string(body) != "telemetry-ok" {
		t.Fatalf(
			"telemetry failed: id=%d status=%d body=%q",
			id,
			status,
			body,
		)
	}

	// StopTest must be accepted even though
	// all regular slots are still occupied.
	if err := writeIPCStressRequest(
		client,
		19,
		"StopTest",
	); err != nil {
		t.Fatal(err)
	}

	seen := make(map[uint32]bool)

	// Expect 16 regular responses plus StopTest.
	// Response order is deliberately unrestricted.
	for i := 0; i < maxRegularRPC+1; i++ {
		id, status, body, err =
			readIPCStressResponse(client)

		if err != nil {
			t.Fatal(err)
		}

		if seen[id] {
			t.Fatalf(
				"duplicate response id=%d",
				id,
			)
		}

		seen[id] = true

		switch {
		case id >= 1 &&
			id <= uint32(maxRegularRPC):

			if status != 0 ||
				string(body) != "regular-ok" {
				t.Fatalf(
					"regular response %d: status=%d body=%q",
					id,
					status,
					body,
				)
			}

		case id == 19:
			if status != 0 ||
				string(body) != "control-ok" {
				t.Fatalf(
					"control response: status=%d body=%q",
					status,
					body,
				)
			}

		default:
			t.Fatalf(
				"unexpected response id=%d",
				id,
			)
		}
	}

	if !seen[19] {
		t.Fatal("no StopTest response")
	}

	// Close the transport and verify serveIPC exits.
	if err := client.Close(); err != nil {
		t.Fatal(err)
	}

	select {
	case err := <-done:
		if !errors.Is(err, io.EOF) {
			t.Fatalf(
				"unexpected IPC server exit: %v",
				err,
			)
		}

	case <-time.After(5 * time.Second):
		t.Fatal("serveIPC did not exit")
	}
}

func TestIPCControlDispatchWhileClientNotReading(
	t *testing.T,
) {
	client, server := net.Pipe()

	entered := make(
		chan struct{},
		maxRegularRPC,
	)

	releaseRegular := make(chan struct{})
	controlEntered := make(chan struct{}, 1)
	done := make(chan error, 1)

	go func() {
		done <- serveIPC(
			server,
			func(method string, _ []byte) ([]byte, error) {
				switch method {
				case "Test":
					entered <- struct{}{}
					<-releaseRegular
					return []byte("ok"), nil

				case "StopTest":
					controlEntered <- struct{}{}
					return []byte("ok"), nil

				default:
					return nil, fmt.Errorf(
						"unexpected method %q",
						method,
					)
				}
			},
		)
	}()

	defer func() {
		_ = client.Close()
		close(releaseRegular)

		select {
		case <-done:
		case <-time.After(2 * time.Second):
			t.Error("IPC server did not exit")
		}
	}()

	if err := client.SetDeadline(
		time.Now().Add(5 * time.Second),
	); err != nil {
		t.Fatal(err)
	}

	// Occupy all regular RPC slots.
	for i := 1; i <= maxRegularRPC; i++ {
		if err := writeIPCStressRequest(
			client,
			uint32(i),
			"Test",
		); err != nil {
			t.Fatal(err)
		}

		select {
		case <-entered:
		case <-time.After(2 * time.Second):
			t.Fatal("regular handler did not start")
		}
	}

	// Request 17 receives RPC_BUSY.
	// Deliberately do not read that response.
	if err := writeIPCStressRequest(
		client,
		17,
		"Test",
	); err != nil {
		t.Fatal(err)
	}

	// Send StopTest while the writer is blocked.
	sent := make(chan error, 1)

	go func() {
		sent <- writeIPCStressRequest(
			client,
			18,
			"StopTest",
		)
	}()

	// The reader must continue dispatching commands,
	// even though the client is not reading responses.
	select {
	case <-controlEntered:
		// StopTest reached its handler.

	case <-time.After(2 * time.Second):
		t.Fatal(
			"StopTest was blocked by an unread RPC_BUSY response",
		)
	}
}
