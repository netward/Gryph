package main

import (
	"context"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/http/httptrace"
	"sync"
	"testing"
	"time"
)

type pr4ReadEvent struct {
	start time.Time
	end   time.Time
	bytes int
}

type pr4ObservedConnection struct {
	mu       sync.Mutex
	accepted time.Time
	active   time.Time
	reads    []pr4ReadEvent
}

type pr4ObservedConn struct {
	net.Conn
	observation *pr4ObservedConnection
}

func (c *pr4ObservedConn) Read(p []byte) (int, error) {
	start := time.Now()
	n, err := c.Conn.Read(p)
	end := time.Now()

	c.observation.mu.Lock()
	if c.observation.active.IsZero() {
		c.observation.reads = append(
			c.observation.reads,
			pr4ReadEvent{
				start: start,
				end:   end,
				bytes: n,
			},
		)
	}
	c.observation.mu.Unlock()

	return n, err
}

type pr4ObservedListener struct {
	net.Listener

	mu          sync.Mutex
	connections map[string]*pr4ObservedConnection
}

func (l *pr4ObservedListener) Accept() (net.Conn, error) {
	conn, err := l.Listener.Accept()
	if err != nil {
		return nil, err
	}

	observation := &pr4ObservedConnection{
		accepted: time.Now(),
	}

	l.mu.Lock()
	l.connections[conn.RemoteAddr().String()] = observation
	l.mu.Unlock()

	return &pr4ObservedConn{
		Conn:        conn,
		observation: observation,
	}, nil
}

func TestPR4HTTPFirstRead(t *testing.T) {
	srv := httptest.NewUnstartedServer(
		http.HandlerFunc(func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			w.Header().Set("Content-Length", "2")
			w.WriteHeader(http.StatusOK)
			_, _ = w.Write([]byte("OK"))
		}),
	)

	listener := &pr4ObservedListener{
		Listener:    srv.Listener,
		connections: make(map[string]*pr4ObservedConnection),
	}

	srv.Listener = listener

	srv.Config.ConnState = func(
		conn net.Conn,
		state http.ConnState,
	) {
		if state != http.StateActive {
			return
		}

		observed, ok := conn.(*pr4ObservedConn)
		if !ok {
			return
		}

		observed.observation.mu.Lock()
		observed.observation.active = time.Now()
		observed.observation.mu.Unlock()
	}

	srv.Start()
	defer srv.Close()

	for i := 0; i < 8; i++ {
		transport := &http.Transport{}

		var traceMu sync.Mutex
		var localAddr string
		var wroteAt time.Time

		trace := &httptrace.ClientTrace{
			GotConn: func(info httptrace.GotConnInfo) {
				traceMu.Lock()
				localAddr = info.Conn.LocalAddr().String()
				traceMu.Unlock()
			},
			WroteRequest: func(info httptrace.WroteRequestInfo) {
				traceMu.Lock()
				wroteAt = time.Now()
				traceMu.Unlock()
			},
		}

		ctx := httptrace.WithClientTrace(
			context.Background(),
			trace,
		)

		req, err := http.NewRequestWithContext(
			ctx,
			http.MethodGet,
			srv.URL,
			nil,
		)
		if err != nil {
			t.Fatal(err)
		}

		client := &http.Client{
			Transport: transport,
			Timeout:   5 * time.Second,
		}

		started := time.Now()
		resp, err := client.Do(req)
		if err != nil {
			transport.CloseIdleConnections()
			t.Fatal(err)
		}

		_, readErr := io.Copy(io.Discard, resp.Body)
		closeErr := resp.Body.Close()
		finished := time.Now()
		transport.CloseIdleConnections()

		if readErr != nil || closeErr != nil {
			t.Fatalf(
				"body read=%v close=%v",
				readErr, closeErr,
			)
		}

		traceMu.Lock()
		addr := localAddr
		wrote := wroteAt
		traceMu.Unlock()

		listener.mu.Lock()
		observation := listener.connections[addr]
		listener.mu.Unlock()

		if observation == nil {
			t.Fatalf("missing connection %q", addr)
		}

		observation.mu.Lock()
		accepted := observation.accepted
		active := observation.active
		reads := append(
			[]pr4ReadEvent(nil),
			observation.reads...,
		)
		observation.mu.Unlock()

		if len(reads) == 0 ||
			active.IsZero() ||
			wrote.IsZero() {
			t.Fatalf(
				"missing events for request %d",
				i+1,
			)
		}

		first := reads[0]
		last := reads[len(reads)-1]

		var totalReadWait time.Duration
		var totalBytes int

		for _, event := range reads {
			totalReadWait += event.end.Sub(event.start)
			totalBytes += event.bytes
		}

		t.Logf(
			"request=%d "+
				"accept=%v "+
				"acceptToRead=%v "+
				"writeToRead=%v "+
				"readWait=%v "+
				"lastReadToActive=%v "+
				"reads=%d bytes=%d "+
				"total=%v",
			i+1,
			accepted.Sub(started),
			first.start.Sub(accepted),
			first.start.Sub(wrote),
			totalReadWait,
			active.Sub(last.end),
			len(reads),
			totalBytes,
			finished.Sub(started),
		)
	}
}
