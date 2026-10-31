package boxmain

import (
	"context"
	"os"
	"os/user"
	"strconv"
	"time"

	"github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/experimental/deprecated"
	"github.com/sagernet/sing-box/include"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing/service"
	"github.com/sagernet/sing/service/filemanager"

	"github.com/spf13/cobra"
)

var (
	globalCtx         context.Context
	configPaths       []string
	configDirectories []string
	workingDir        string
	disableColor      bool
)

var mainCommand = &cobra.Command{
	Use:              "sing-box",
	PersistentPreRun: preRun,
}

func init() {
	mainCommand.PersistentFlags().StringArrayVarP(&configPaths, "config", "c", nil, "set configuration file path")
	mainCommand.PersistentFlags().StringArrayVarP(&configDirectories, "config-directory", "C", nil, "set configuration directory path")
	mainCommand.PersistentFlags().StringVarP(&workingDir, "directory", "D", "", "set working directory")
	mainCommand.PersistentFlags().BoolVarP(&disableColor, "disable-color", "", false, "disable color output")
}

func newRuntimeContext() context.Context {
	ctx := context.Background()

	sudoUser := os.Getenv("SUDO_USER")
	sudoUID, _ := strconv.Atoi(os.Getenv("SUDO_UID"))
	sudoGID, _ := strconv.Atoi(os.Getenv("SUDO_GID"))

	if sudoUID == 0 && sudoGID == 0 && sudoUser != "" {
		sudoUserObject, err := user.Lookup(sudoUser)
		if err == nil {
			sudoUID, _ = strconv.Atoi(sudoUserObject.Uid)
			sudoGID, _ = strconv.Atoi(sudoUserObject.Gid)
		}
	}

	if sudoUID > 0 && sudoGID > 0 {
		ctx = filemanager.WithDefault(
			ctx, "", "", sudoUID, sudoGID,
		)
	}

	ctx = service.ContextWith(
		ctx,
		deprecated.NewStderrManager(log.StdLogger()),
	)

	return box.Context(
		ctx,
		include.InboundRegistry(),
		include.OutboundRegistry(),
		include.EndpointRegistry(),
		include.DNSTransportRegistry(),
		include.ServiceRegistry(),
	)
}

func preRun(cmd *cobra.Command, args []string) {
	if disableColor {
		log.SetStdLogger(
			log.NewDefaultFactory(
				context.Background(),
				log.Formatter{
					BaseTime:      time.Now(),
					DisableColors: true,
				},
				os.Stderr,
				"",
				nil,
				false,
			).Logger(),
		)
	}

	ctx := newRuntimeContext()

	if workingDir != "" {
		_, err := os.Stat(workingDir)
		if err != nil {
			filemanager.MkdirAll(ctx, workingDir, 0o777)
		}

		if err := os.Chdir(workingDir); err != nil {
			log.Fatal(err)
		}
	}

	if len(configPaths) == 0 &&
		len(configDirectories) == 0 {
		configPaths = append(configPaths, "config.json")
	}

	globalCtx = ctx
}
