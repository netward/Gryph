package test_utils

import (
	"context"
	"errors"
	"github.com/Mahdi-zarei/speedtest-go/speedtest"
	"github.com/sagernet/sing/common/metadata"
	"net"
	"time"
	"sync"
)


var (
    testContextMu sync.RWMutex
    activeTestContext context.Context
    activeTestCancel context.CancelFunc
)

// TestContext returns a safe snapshot of the current test context.
func TestContext() context.Context {
    testContextMu.RLock()
    ctx := activeTestContext
    testContextMu.RUnlock()

    if ctx == nil {
        return context.Background()
    }
    return ctx
}

// ResetTestContext cancels previous tests and prepares a fresh context.
func ResetTestContext() {
    testContextMu.Lock()
    defer testContextMu.Unlock()

    if activeTestCancel != nil {
        activeTestCancel()
    }
    activeTestContext, activeTestCancel = context.WithCancel(context.Background())
}


const FetchServersTimeout = 8 * time.Second
const MaxConcurrentTests = 100

func getNetDialer(dialer func(ctx context.Context, network string, destination metadata.Socksaddr) (net.Conn, error)) func(ctx context.Context, network string, address string) (net.Conn, error) {
	return func(ctx context.Context, network string, address string) (net.Conn, error) {
		return dialer(ctx, network, metadata.ParseSocksaddr(address))
	}
}

func getSpeedtestServer(ctx context.Context, dialer func(ctx context.Context, network string, address string) (net.Conn, error)) (*speedtest.Server, error) {
	clt := speedtest.New(speedtest.WithUserConfig(&speedtest.UserConfig{
		DialContextFunc: dialer,
		PingMode:        speedtest.HTTP,
		MaxConnections:  8,
	}))
	fetchCtx, cancel := context.WithTimeout(ctx, FetchServersTimeout)
	defer cancel()
	srv, err := clt.FetchServerListContext(fetchCtx)
	if err != nil {
		return nil, err
	}
	srv, err = srv.FindServer(nil)
	if err != nil {
		return nil, err
	}

	if srv.Len() == 0 {
		return nil, errors.New("no server found for speedTest")
	}

	return srv[0], nil
}
