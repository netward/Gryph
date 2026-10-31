package main

import (
	"GryphCore/gen"
	"GryphCore/internal/boxbox"
	"GryphCore/internal/boxmain"
	"GryphCore/internal/process"
	"GryphCore/internal/sys"
	"GryphCore/internal/wg"
	"GryphCore/internal/xray"
	"GryphCore/test_utils"
	"context"
	"errors"
	"fmt"
	"log"
	"net/netip"
	"os"
	"runtime"
	"strings"
	"sync"
	"time"

	"github.com/google/shlex"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/experimental/clashapi"
	E "github.com/sagernet/sing/common/exceptions"
	"github.com/sagernet/sing/service"
	"github.com/xtls/xray-core/core"
)

var boxInstance *boxbox.Box
var extraProcess *process.Process
var needUnsetDNS bool
var instanceCancel context.CancelFunc
var debug bool

// Xray core
var xrayInstance *core.Instance

// Serializes Start and Stop operations.
var coreLifecycleMu sync.RWMutex

type server struct {
	gen.UnimplementedLibcoreServiceServer
}

// To returns a pointer to the given value.
func To[T any](v T) *T {
	return &v
}

func (s *server) Start(
	ctx context.Context,
	in *gen.LoadConfigReq,
) (out *gen.ErrorResp, _ error) {
	coreLifecycleMu.Lock()
	defer coreLifecycleMu.Unlock()
	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = To(err.Error())
		}
	}()

	if debug {
		log.Println("Start:", in.GetCoreConfig())
	}

	if boxInstance != nil || xrayInstance != nil || extraProcess != nil {
		err = errors.New("instance already started")
		return
	}

	// Roll back resources if startup fails.
	defer func() {
		if err == nil {
			return
		}

		if boxInstance != nil {
			boxInstance.CloseWithTimeout(
				instanceCancel,
				2*time.Second,
				log.Println,
				true,
			)
			boxInstance = nil
		}

		if instanceCancel != nil {
			instanceCancel()
			instanceCancel = nil
		}

		if xrayInstance != nil {
			if closeErr := xrayInstance.Close(); closeErr != nil {
				log.Println("Failed to roll back Xray:", closeErr)
			}
			xrayInstance = nil
		}

		if extraProcess != nil {
			extraProcess.Stop()
			extraProcess = nil
		}
	}()

	if in.GetNeedExtraProcess() {
		args, e := shlex.Split(in.GetExtraProcessArgs())
		if e != nil {
			err = E.Cause(e, "Failed to parse args")
			return
		}
		var extraConfPath, extraCleanupPath string
		if in.ExtraProcessConf != nil {
			// The Core (not the GUI) creates the config, in a fresh randomly
			// named temp file that cannot be hijacked by symlink/pre-existing
			// file tricks even when the Core is elevated. See CreateExtraConfig.
			extraConfPath, extraCleanupPath, e = process.CreateExtraConfig(in.GetExtraProcessConf())
			if e != nil {
				err = E.Cause(e, "Failed to create extra.conf")
				return
			}
			for idx, arg := range args {
				if strings.Contains(arg, "%s") {
					args[idx] = fmt.Sprintf(arg, extraConfPath)
					break
				}
			}
		}

		extraProcess = process.NewProcess(
			in.GetExtraProcessPath(),
			args,
			in.GetExtraNoOut(),
		)
		extraProcess.SetCleanupPath(extraCleanupPath)
		err = extraProcess.Start()
		if err != nil {
			return
		}
	}

	if in.GetNeedXray() {
		xrayInstance, err = xray.CreateXrayInstance(
			in.GetXrayConfig(),
		)

		if err != nil {
			return
		}

		err = xrayInstance.Start()
		if err != nil {
			return
		}
	}

	boxInstance, instanceCancel, err = boxmain.Create(
		[]byte(in.GetCoreConfig()),
	)
	if err != nil {
		if extraProcess != nil {
			extraProcess.Stop()
			extraProcess = nil
		}
		if xrayInstance != nil {
			xrayInstance.Close()
			xrayInstance = nil
		}
		return
	}

	if runtime.GOOS == "darwin" && in.GetTunIpv4Cidr() != "" {
		stopAllCores := func() {
			boxInstance.CloseWithTimeout(instanceCancel, time.Second*2, log.Println, true)
			boxInstance = nil
			if extraProcess != nil {
				extraProcess.Stop()
				extraProcess = nil
			}
			if xrayInstance != nil {
				xrayInstance.Close()
				xrayInstance = nil
			}
		}

		tunCIDR := in.GetTunIpv4Cidr()
		tunPrefix, parseErr := netip.ParsePrefix(tunCIDR)
		if parseErr != nil || !tunPrefix.Addr().Is4() {
			err = fmt.Errorf("invalid tun_ipv4_cidr %q", tunCIDR)
			stopAllCores()
			return
		}

		tunDNS := tunPrefix.Addr().Next()
		if !tunDNS.IsValid() || !tunDNS.Is4() {
			err = fmt.Errorf("got invalid DNS IP from tun_ipv4_cidr: %s", tunDNS)
			stopAllCores()
			return
		}

		if err := sys.SetSystemDNS(tunDNS.String(), boxInstance.Network().InterfaceMonitor()); err != nil {
			log.Println("Failed to set system DNS:", err)
		}

		needUnsetDNS = true
	}

	return
}

func (s *server) Stop(
	ctx context.Context,
	in *gen.EmptyReq,
) (out *gen.ErrorResp, _ error) {
	coreLifecycleMu.Lock()
	defer coreLifecycleMu.Unlock()
	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = To(err.Error())
		}
	}()

	// Stop new users from acquiring the running instance.
	// coreLifecycleMu.Lock() is already held here.
	if boxInstance != nil {
		waitCtx, cancel := context.WithTimeout(
			ctx,
			10*time.Second,
		)

		waitErr := currentCoreTests.cancelAndWait(waitCtx)
		cancel()

		if waitErr != nil {
			err = fmt.Errorf(
				"cannot stop core: active tests did not finish: %w",
				waitErr,
			)
			return
		}
	}

	if boxInstance != nil {
		if needUnsetDNS {
			needUnsetDNS = false

			if e := sys.SetSystemDNS(
				"Empty",
				boxInstance.Network().InterfaceMonitor(),
			); e != nil {
				log.Println("Failed to unset system DNS:", e)
			}
		}

		boxInstance.CloseWithTimeout(
			instanceCancel,
			2*time.Second,
			log.Println,
			true,
		)

		boxInstance = nil
	}

	if instanceCancel != nil {
		instanceCancel()
		instanceCancel = nil
	}

	if extraProcess != nil {
		extraProcess.Stop()
		extraProcess = nil
	}

	if xrayInstance != nil {
		if e := xrayInstance.Close(); e != nil {
			log.Println("Failed to close Xray:", e)
		}
		xrayInstance = nil
	}

	return
}

func (s *server) CheckConfig(ctx context.Context, in *gen.LoadConfigReq) (out *gen.ErrorResp, _ error) {
	out = &gen.ErrorResp{}
	// Recover from panics inside boxmain.Check (e.g. malformed configs that trigger
	// sing-box internal panics). Without this, the panic propagates to main() which
	// calls os.Exit(0) and kills the entire core process. The full goroutine stack
	// goes to the operator log; the wire response carries only the panic value.
	defer func() {
		if r := recover(); r != nil {
			buf := make([]byte, 4096)
			n := runtime.Stack(buf, false)
			log.Printf("CheckConfig panic: %v\n%s", r, buf[:n])
			out.Error = To(fmt.Sprintf("CheckConfig panic: %v", r))
		}
	}()
	err := boxmain.Check([]byte(in.GetCoreConfig()))
	if err != nil {
		out.Error = To(err.Error())
	}
	return
}

func (s *server) Test(ctx context.Context, in *gen.TestReq) (*gen.TestResp, error) {
	var testInstance *boxbox.Box
	var twice = true
	testCtx := test_utils.TestContext()

	if in.GetTestCurrent() {
		var release func()

		testInstance, testCtx, release = acquireCurrentCoreTest()

		if testInstance == nil {
			return &gen.TestResp{
				Results: []*gen.URLTestResp{{
					OutboundTag: To("proxy"),
					LatencyMs:   To(int32(0)),
					Error:       To("Instance is not running"),
				}},
			}, nil
		}

		defer release()
		twice = false
	} else {
		tempCtx, releaseTemp := temporaryTests.begin()
		testCtx = tempCtx
		defer releaseTemp()
		runtime, err := newTemporaryTestRuntime(
			in.GetConfig(),
			in.GetNeedXray(),
			in.GetXrayConfig(),
		)
		if err != nil {
			return nil, err
		}

		testInstance = runtime.box

		defer func() {
			if err := runtime.Close(); err != nil {
				log.Printf("Test cleanup failed: %v", err)
			}
		}()
	}

	outboundTags := in.OutboundTags
	if in.GetUseDefaultOutbound() || in.GetTestCurrent() {
		outbound := testInstance.Outbound().Default()
		outboundTags = []string{outbound.Tag()}
	}

	var maxConcurrency = in.GetMaxConcurrency()
	if maxConcurrency >= 500 || maxConcurrency == 0 {
		maxConcurrency = test_utils.MaxConcurrentTests
	}
	results := test_utils.BatchURLTest(testCtx, testInstance, outboundTags, in.GetUrl(), int(maxConcurrency), twice, time.Duration(in.GetTestTimeoutMs())*time.Millisecond)

	res := make([]*gen.URLTestResp, 0)
	for idx, data := range results {
		errStr := ""
		if data.Error != nil {
			errStr = data.Error.Error()
		}
		res = append(res, &gen.URLTestResp{
			OutboundTag: To(outboundTags[idx]),
			LatencyMs:   To(int32(data.Duration.Milliseconds())),
			Error:       To(errStr),
		})
	}

	return &gen.TestResp{Results: res}, nil
}

func (s *server) StopTest(
	ctx context.Context,
	in *gen.EmptyReq,
) (*gen.EmptyResp, error) {
	waitCtx, cancel := context.WithTimeout(
		ctx,
		10*time.Second,
	)
	defer cancel()

	if err := temporaryTests.cancelAndWait(waitCtx); err != nil {
		return nil, fmt.Errorf(
			"temporary tests did not finish: %w",
			err,
		)
	}

	return &gen.EmptyResp{}, nil
}

func (s *server) QueryURLTest(ctx context.Context, in *gen.EmptyReq) (out *gen.QueryURLTestResponse, _ error) {
	results := test_utils.URLReporter.Results()
	out = &gen.QueryURLTestResponse{}
	for _, r := range results {
		errStr := ""
		if r.Error != nil {
			errStr = r.Error.Error()
		}
		out.Results = append(out.Results, &gen.URLTestResp{
			OutboundTag: To(r.Tag),
			LatencyMs:   To(int32(r.Duration.Milliseconds())),
			Error:       To(errStr),
		})
	}
	return
}

func (s *server) IPTest(
	ctx context.Context,
	in *gen.IPTestRequest,
) (*gen.IPTestResp, error) {
	testCtx, releaseTemp := temporaryTests.begin()
	defer releaseTemp()
	runtime, err := newTemporaryTestRuntime(
		in.GetConfig(),
		in.GetNeedXray(),
		in.GetXrayConfig(),
	)
	if err != nil {
		return nil, err
	}

	testInstance := runtime.box

	defer func() {
		if err := runtime.Close(); err != nil {
			log.Printf("IPTest cleanup failed: %v", err)
		}
	}()

	outboundTags := in.OutboundTags
	if in.GetUseDefaultOutbound() {
		outbound := testInstance.Outbound().Default()
		outboundTags = []string{outbound.Tag()}
	}

	maxConcurrency := in.GetMaxConcurrency()
	if maxConcurrency >= 500 || maxConcurrency == 0 {
		maxConcurrency = test_utils.MaxConcurrentTests
	}
	timeout := time.Duration(in.GetTestTimeoutMs()) * time.Millisecond
	results := test_utils.BatchIPTest(testCtx, testInstance, outboundTags, int(maxConcurrency), timeout)

	res := make([]*gen.IPTestRes, 0, len(results))
	for idx, data := range results {
		errStr := ""
		if data.Error != nil {
			errStr = data.Error.Error()
		}
		tag := outboundTags[idx]
		res = append(res, &gen.IPTestRes{
			OutboundTag: To(tag),
			Ip:          To(data.Result.IP),
			CountryCode: To(data.Result.CountryCode),
			Error:       To(errStr),
		})
	}
	return &gen.IPTestResp{Results: res}, nil
}

func (s *server) QueryIPTest(ctx context.Context, in *gen.EmptyReq) (out *gen.QueryIPTestResponse, _ error) {
	results := test_utils.IPReporter.Results()
	out = &gen.QueryIPTestResponse{}
	for _, r := range results {
		errStr := ""
		if r.Error != nil {
			errStr = r.Error.Error()
		}
		out.Results = append(out.Results, &gen.IPTestRes{
			OutboundTag: To(r.Tag),
			Ip:          To(r.Result.IP),
			CountryCode: To(r.Result.CountryCode),
			Error:       To(errStr),
		})
	}
	return
}

func (s *server) QueryStats(
	ctx context.Context,
	in *gen.EmptyReq,
) (out *gen.QueryStatsResp, err error) {
	coreLifecycleMu.RLock()
	defer coreLifecycleMu.RUnlock()

	out = &gen.QueryStatsResp{}
	out.Ups = make(map[string]int64)
	out.Downs = make(map[string]int64)
	if boxInstance != nil {
		clash := service.FromContext[adapter.ClashServer](boxInstance.Context())
		if clash != nil {
			cApi, ok := clash.(*clashapi.Server)
			if !ok {
				log.Println("Failed to assert clash server")
				err = E.New("invalid clash server type")
				return
			}
			outbounds := service.FromContext[adapter.OutboundManager](boxInstance.Context())
			if outbounds == nil {
				log.Println("Failed to get outbound manager")
				err = E.New("nil outbound manager")
				return
			}
			endpoints := service.FromContext[adapter.EndpointManager](boxInstance.Context())
			if endpoints == nil {
				log.Println("Failed to get endpoint manager")
				err = E.New("nil endpoint manager")
				return
			}
			for _, ob := range outbounds.Outbounds() {
				u, d := cApi.TrafficManager().TotalOutbound(ob.Tag())
				out.Ups[ob.Tag()] = u
				out.Downs[ob.Tag()] = d
			}
			for _, ep := range endpoints.Endpoints() {
				u, d := cApi.TrafficManager().TotalOutbound(ep.Tag())
				out.Ups[ep.Tag()] = u
				out.Downs[ep.Tag()] = d
			}
		}
	}
	return
}

func (s *server) ListConnections(
	ctx context.Context,
	in *gen.EmptyReq,
) (*gen.ListConnectionsResp, error) {
	coreLifecycleMu.RLock()
	defer coreLifecycleMu.RUnlock()

	if boxInstance == nil {
		return &gen.ListConnectionsResp{}, nil
	}
	if service.FromContext[adapter.ClashServer](boxInstance.Context()) == nil {
		return &gen.ListConnectionsResp{}, errors.New("no clash server found")
	}
	clash, ok := service.FromContext[adapter.ClashServer](boxInstance.Context()).(*clashapi.Server)
	if !ok {
		return &gen.ListConnectionsResp{}, errors.New("invalid state, should not be here")
	}
	connections := clash.TrafficManager().Connections()

	res := make([]*gen.ConnectionMetaData, 0)
	for _, c := range connections {
		process := ""
		if c.Metadata.ProcessInfo != nil {
			spl := strings.Split(c.Metadata.ProcessInfo.ProcessPath, string(os.PathSeparator))
			process = spl[len(spl)-1]
		}
		r := &gen.ConnectionMetaData{
			Id:        To(c.ID.String()),
			CreatedAt: To(c.CreatedAt.UnixMilli()),
			Upload:    To(c.Upload.Load()),
			Download:  To(c.Download.Load()),
			Outbound:  To(c.Outbound),
			Network:   To(c.Metadata.Network),
			Dest:      To(c.Metadata.Destination.String()),
			Protocol:  To(c.Metadata.Protocol),
			Domain:    To(c.Metadata.Domain),
			Process:   To(process),
		}
		res = append(res, r)
	}
	return &gen.ListConnectionsResp{Connections: res}, nil
}

func (s *server) IsPrivileged(ctx context.Context, _ *gen.EmptyReq) (*gen.IsPrivilegedResponse, error) {
	if runtime.GOOS == "windows" {
		return &gen.IsPrivilegedResponse{
			HasPrivilege: To(false),
		}, nil
	}

	return &gen.IsPrivilegedResponse{HasPrivilege: To(os.Geteuid() == 0)}, nil
}

func (s *server) SpeedTest(ctx context.Context, in *gen.SpeedTestRequest) (*gen.SpeedTestResponse, error) {
	if !in.GetTestDownload() &&
		!in.GetTestUpload() &&
		!in.GetSimpleDownload() &&
		!in.GetOnlyCountry() {
		return nil, errors.New("cannot run empty test")
	}
	var testInstance *boxbox.Box
	outboundTags := in.OutboundTags
	testCtx := test_utils.TestContext()

	if in.GetTestCurrent() {
		var release func()

		testInstance, testCtx, release = acquireCurrentCoreTest()

		if testInstance == nil {
			return &gen.SpeedTestResponse{
				Results: []*gen.SpeedTestResult{{
					OutboundTag: To("proxy"),
					Error:       To("Instance is not running"),
				}},
			}, nil
		}
		defer release()
	} else {
		tempCtx, releaseTemp := temporaryTests.begin()
		testCtx = tempCtx
		defer releaseTemp()
		runtime, err := newTemporaryTestRuntime(
			in.GetConfig(),
			in.GetNeedXray(),
			in.GetXrayConfig(),
		)
		if err != nil {
			return nil, err
		}

		testInstance = runtime.box

		defer func() {
			if err := runtime.Close(); err != nil {
				log.Printf("SpeedTest cleanup failed: %v", err)
			}
		}()
	}

	if in.GetUseDefaultOutbound() || in.GetTestCurrent() {
		outbound := testInstance.Outbound().Default()
		outboundTags = []string{outbound.Tag()}
	}

	results := test_utils.BatchSpeedTest(
		testCtx,
		testInstance,
		outboundTags,
		in.GetTestDownload(),
		in.GetTestUpload(),
		in.GetSimpleDownload(),
		in.GetSimpleDownloadAddr(),
		time.Duration(in.GetTimeoutMs())*time.Millisecond,
		in.GetOnlyCountry(),
		in.GetCountryConcurrency(),
	)

	res := make([]*gen.SpeedTestResult, 0)
	for _, data := range results {
		errStr := ""
		if data.Error != nil {
			errStr = data.Error.Error()
		}
		res = append(res, &gen.SpeedTestResult{
			DlSpeed:       To(data.DlSpeed),
			UlSpeed:       To(data.UlSpeed),
			Latency:       To(data.Latency),
			OutboundTag:   To(data.Tag),
			Error:         To(errStr),
			ServerName:    To(data.ServerName),
			ServerCountry: To(data.ServerCountry),
			Cancelled:     To(data.Cancelled),
		})
	}

	return &gen.SpeedTestResponse{Results: res}, nil
}

func (s *server) QuerySpeedTest(context.Context, *gen.EmptyReq) (*gen.QuerySpeedTestResponse, error) {
	res, isRunning := test_utils.SpTQuerier.Result()
	errStr := ""
	if res.Error != nil {
		errStr = res.Error.Error()
	}
	return &gen.QuerySpeedTestResponse{
		Result: &gen.SpeedTestResult{
			DlSpeed:       To(res.DlSpeed),
			UlSpeed:       To(res.UlSpeed),
			Latency:       To(res.Latency),
			OutboundTag:   To(res.Tag),
			Error:         To(errStr),
			ServerName:    To(res.ServerName),
			ServerCountry: To(res.ServerCountry),
			Cancelled:     To(res.Cancelled),
		},
		IsRunning: To(isRunning),
	}, nil
}

func (s *server) QueryCountryTest(ctx context.Context, _ *gen.EmptyReq) (out *gen.QueryCountryTestResponse, _ error) {
	results := test_utils.CountryResults.Results()
	out = &gen.QueryCountryTestResponse{}
	for _, res := range results {
		var errStr string
		if res.Error != nil {
			errStr = res.Error.Error()
		}
		out.Results = append(out.Results, &gen.SpeedTestResult{
			DlSpeed:       To(res.DlSpeed),
			UlSpeed:       To(res.UlSpeed),
			Latency:       To(res.Latency),
			OutboundTag:   To(res.Tag),
			Error:         To(errStr),
			ServerName:    To(res.ServerName),
			ServerCountry: To(res.ServerCountry),
			Cancelled:     To(res.Cancelled),
		})
	}
	return
}

func (s *server) GenWgKeyPair(ctx context.Context, _ *gen.EmptyReq) (out *gen.GenWgKeyPairResponse, _ error) {
	var res gen.GenWgKeyPairResponse
	privateKey, err := wg.GeneratePrivateKey()
	if err != nil {
		res.Error = To(err.Error())
		return &res, nil
	}
	res.PrivateKey = To(privateKey.String())
	res.PublicKey = To(privateKey.PublicKey().String())
	return &res, nil
}
