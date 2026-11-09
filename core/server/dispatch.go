package main

import (
	"GryphCore/gen"
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"log"
	"net"
	"sync"

	"google.golang.org/protobuf/proto"
)

var globalServer = &server{}

const (
	maxRPCPayloadBytes uint32 = 16 << 20
	maxRPCMethodBytes  uint16 = 128
)

// readIPCRequest validates lengths before allocating memory.
func readIPCRequest(r io.Reader) (
	uint32, string, []byte, error,
) {
	var requestID uint32
	if err := binary.Read(
		r, binary.LittleEndian, &requestID,
	); err != nil {
		return 0, "", nil, err
	}

	var methodLen uint16
	if err := binary.Read(
		r, binary.LittleEndian, &methodLen,
	); err != nil {
		return 0, "", nil, err
	}

	if methodLen == 0 || methodLen > maxRPCMethodBytes {
		return 0, "", nil, fmt.Errorf(
			"invalid IPC method length: %d", methodLen,
		)
	}

	methodBytes := make([]byte, int(methodLen))
	if _, err := io.ReadFull(r, methodBytes); err != nil {
		return 0, "", nil, err
	}

	var payloadLen uint32
	if err := binary.Read(
		r, binary.LittleEndian, &payloadLen,
	); err != nil {
		return 0, "", nil, err
	}

	if payloadLen > maxRPCPayloadBytes {
		return 0, "", nil, fmt.Errorf(
			"IPC payload too large: %d bytes", payloadLen,
		)
	}

	payload := make([]byte, int(payloadLen))
	if _, err := io.ReadFull(r, payload); err != nil {
		return 0, "", nil, err
	}

	return requestID, string(methodBytes), payload, nil
}

func writeIPCAll(w io.Writer, data []byte) error {
	for len(data) > 0 {
		n, err := w.Write(data)

		if n > 0 {
			data = data[n:]
		}

		if err != nil {
			return err
		}

		if n == 0 {
			return io.ErrShortWrite
		}
	}

	return nil
}

// runDispatch reads request frames and dispatches each one to its own goroutine.
// Responses are written back with the matching reqId so the GUI can demultiplex.
//
// Request wire format (little-endian):
//
//	[uint32 reqId][uint16 methodLen][method bytes][uint32 payloadLen][payload]
//
// Response wire format (little-endian):
//
//	[uint32 reqId][uint8 status][uint32 dataLen][data]
func runDispatch(conn net.Conn) {
	defer func() {
		conn.Close()
		log.Fatal("IPC connection dropped, exiting")
	}()

	var writeMu sync.Mutex

	writeResponse := func(reqID uint32, status uint8, data []byte) {
		if len(data) > int(maxRPCPayloadBytes) {
			status = 1
			data = []byte("RPC response exceeds payload limit")
		}

		var header [9]byte
		binary.LittleEndian.PutUint32(header[0:], reqID)
		header[4] = status
		binary.LittleEndian.PutUint32(
			header[5:], uint32(len(data)),
		)
		log.Printf(
		    "[IPC][TX] requestId=%d status=%d payloadLen=%d header=%x",
		    reqID,
		    status,
		    len(data),
		    header,
		)

		writeMu.Lock()
		defer writeMu.Unlock()

		if err := writeIPCAll(conn, header[:]); err != nil {
			log.Printf("[IPC] response header write failed: %v", err)
			_ = conn.Close()
			return
		}

		if err := writeIPCAll(conn, data); err != nil {
			log.Printf("[IPC] response body write failed: %v", err)
			_ = conn.Close()
		}
	}

	for {
		requestID, method, payload, err := readIPCRequest(conn)
		if err != nil {
			if err != io.EOF {
				log.Printf("[IPC] request read failed: %v", err)
			}
			return
		}

		// Existing concurrent dispatch behavior.
		// This is intentionally unchanged in PR-4.9A.
		go func(id uint32, method string, pl []byte) {
			respData, dispatchErr := dispatch(method, pl)

			if dispatchErr != nil {
				writeResponse(
					id, 1, []byte(dispatchErr.Error()),
				)
			} else {
				writeResponse(id, 0, respData)
			}
		}(requestID, method, payload)
	}
}

func dispatch(methodName string, payload []byte) ([]byte, error) {
	ctx := context.Background()
	s := globalServer

	switch methodName {
	case "Start":
		req := &gen.LoadConfigReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.Start(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "Stop":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.Stop(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "CheckConfig":
		req := &gen.LoadConfigReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.CheckConfig(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "Test":
		req := &gen.TestReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.Test(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "StopTest":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.StopTest(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "QueryURLTest":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.QueryURLTest(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "IPTest":
		req := &gen.IPTestRequest{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.IPTest(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "QueryIPTest":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.QueryIPTest(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "QueryStats":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.QueryStats(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "ListConnections":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.ListConnections(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "IsPrivileged":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.IsPrivileged(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "SetSystemDNS":
		req := &gen.SetSystemDNSRequest{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.SetSystemDNS(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "SpeedTest":
		req := &gen.SpeedTestRequest{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.SpeedTest(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "QuerySpeedTest":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.QuerySpeedTest(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "QueryCountryTest":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.QueryCountryTest(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	case "GenWgKeyPair":
		req := &gen.EmptyReq{}
		if err := proto.Unmarshal(payload, req); err != nil {
			return nil, err
		}
		resp, err := s.GenWgKeyPair(ctx, req)
		if err != nil {
			return nil, err
		}
		return proto.Marshal(resp)

	default:
		return nil, fmt.Errorf("unknown method: %s", methodName)
	}
}
