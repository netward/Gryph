package main

import (
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