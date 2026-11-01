package main

import (
	"bytes"
	"encoding/binary"
	"strings"
	"testing"

	"GryphCore/gen"
	"google.golang.org/protobuf/proto"
)

func TestDispatchUnknownMethod(t *testing.T) {
	_, err := dispatch("NotAnRPCMethod", nil)

	if err == nil ||
		!strings.Contains(err.Error(), "unknown method") {
		t.Fatalf("expected unknown-method error, got %v", err)
	}
}

func TestDispatchRejectsMalformedProtobuf(t *testing.T) {
	malformed := []byte{0xff}

	methods := []string{
		"Start",
		"Stop",
		"CheckConfig",
		"QueryStats",
		"ListConnections",
	}

	for _, method := range methods {
		t.Run(method, func(t *testing.T) {
			_, err := dispatch(method, malformed)

			if err == nil {
				t.Fatalf(
					"%s accepted malformed protobuf",
					method,
				)
			}
		})
	}
}

func TestDispatchQueryStatsWhileStopped(t *testing.T) {
	if boxInstance != nil {
		t.Skip("runtime is already active")
	}

	request, err := proto.Marshal(&gen.EmptyReq{})
	if err != nil {
		t.Fatal(err)
	}

	payload, err := dispatch("QueryStats", request)
	if err != nil {
		t.Fatalf(
			"QueryStats failed on stopped core: %v",
			err,
		)
	}

	var response gen.QueryStatsResp

	if err := proto.Unmarshal(payload, &response); err != nil {
		t.Fatalf(
			"invalid QueryStats protobuf response: %v",
			err,
		)
	}

	if len(response.GetUps()) != 0 ||
		len(response.GetDowns()) != 0 {
		t.Fatalf(
			"stopped core returned traffic: ups=%v downs=%v",
			response.GetUps(),
			response.GetDowns(),
		)
	}
}

func TestIPCRejectsOversizedPayload(t *testing.T) {
	var buf bytes.Buffer

	_ = binary.Write(
		&buf, binary.LittleEndian, uint32(1),
	)
	_ = binary.Write(
		&buf, binary.LittleEndian, uint16(4),
	)
	_, _ = buf.WriteString("Test")
	_ = binary.Write(
		&buf,
		binary.LittleEndian,
		maxRPCPayloadBytes+1,
	)

	_, _, _, err := readIPCRequest(&buf)

	if err == nil {
		t.Fatal("oversized IPC payload was accepted")
	}
}

func TestIPCRejectsTruncatedPayload(t *testing.T) {
	var buf bytes.Buffer

	_ = binary.Write(
		&buf, binary.LittleEndian, uint32(2),
	)
	_ = binary.Write(
		&buf, binary.LittleEndian, uint16(4),
	)
	_, _ = buf.WriteString("Test")
	_ = binary.Write(
		&buf, binary.LittleEndian, uint32(10),
	)

	// Advertise 10 bytes, but send only 3.
	_, _ = buf.Write([]byte{1, 2, 3})

	_, _, _, err := readIPCRequest(&buf)

	if err == nil {
		t.Fatal("truncated IPC payload was accepted")
	}
}
