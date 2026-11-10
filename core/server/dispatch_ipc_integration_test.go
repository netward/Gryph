package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"testing"
	"time"
)

// Tests a real request/response frame over net.Pipe.
func TestIPCWireRoundTrip(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()

	done := make(chan error, 1)

	go func() {
		done <- serveIPC(
			server,
			func(method string, payload []byte) ([]byte, error) {
				if method != "Echo" {
					return nil, errors.New("unexpected method")
				}

				return append([]byte(nil), payload...), nil
			},
		)
	}()

	if err := client.SetDeadline(
		time.Now().Add(5 * time.Second),
	); err != nil {
		t.Fatal(err)
	}

	// Construct the request frame.
	var request bytes.Buffer

	_ = binary.Write(
		&request,
		binary.LittleEndian,
		uint32(42),
	)

	_ = binary.Write(
		&request,
		binary.LittleEndian,
		uint16(4),
	)

	request.WriteString("Echo")

	payload := []byte("gryph-ipc-test")

	_ = binary.Write(
		&request,
		binary.LittleEndian,
		uint32(len(payload)),
	)

	request.Write(payload)

	if err := writeIPCAll(
		client,
		request.Bytes(),
	); err != nil {
		t.Fatalf("write request: %v", err)
	}

	// Read the exact 9-byte response header.
	var header [9]byte

	if _, err := io.ReadFull(client, header[:]); err != nil {
		t.Fatalf("read response header: %v", err)
	}

	requestID := binary.LittleEndian.Uint32(
		header[0:4],
	)
	status := header[4]
	payloadLen := binary.LittleEndian.Uint32(
		header[5:9],
	)

	if requestID != 42 {
		t.Fatalf(
			"unexpected request ID: %d",
			requestID,
		)
	}

	if status != 0 {
		t.Fatalf(
			"unexpected RPC status: %d",
			status,
		)
	}

	if payloadLen != uint32(len(payload)) {
		t.Fatalf(
			"unexpected payload length: %d",
			payloadLen,
		)
	}

	// Read the response payload.
	response := make([]byte, int(payloadLen))

	if _, err := io.ReadFull(client, response); err != nil {
		t.Fatalf("read response payload: %v", err)
	}

	if !bytes.Equal(response, payload) {
		t.Fatalf(
			"payload mismatch: got %q, want %q",
			response,
			payload,
		)
	}

	// Disconnect the client.
	client.Close()

	select {
	case err := <-done:
		if !errors.Is(err, io.EOF) {
			t.Fatalf(
				"unexpected server exit: %v",
				err,
			)
		}

	case <-time.After(5 * time.Second):
		t.Fatal("IPC server did not exit")
	}
}
