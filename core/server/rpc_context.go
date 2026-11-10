package main

import "context"

// bindRPCContext cancels an operation when either
// its own context or the RPC session is cancelled.
func bindRPCContext(
	operationCtx context.Context,
	rpcCtx context.Context,
) (context.Context, func()) {
	ctx, cancel := context.WithCancel(operationCtx)

	stop := context.AfterFunc(rpcCtx, cancel)

	release := func() {
		stop()
		cancel()
	}

	return ctx, release
}
