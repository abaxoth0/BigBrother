package app

import "bigbrother_server_backend/packages/common/log"

func StartInit() {
	if err := log.DefaultLogger.AddForwarding(log.Stdout); err != nil {
		panic(err.Error())
	}
}

func EndInit() {
	// if !config.App.ShowLogs {
	// 	if err := log.DefaultLogger.RemoveForwarding(log.Stdout); err != nil {
	// 		panic(err.Error())
	// 	}
	// }
}

func InitDefaults() {
	// config.Init()

	appLogger.Info("Initializing default logger...", nil)

	// logger.DefaultLoggerConfig.Debug = config.Debug.Enabled
	// logger.DefaultLoggerConfig.Trace = config.App.TraceLogsEnabled
	// logger.DefaultLoggerConfig.AppInstance = config.App.ServiceID

	if err := log.DefaultLogger.Init(); err != nil {
		appLogger.Fatal("Failed to initalize default logger", err.Error(), nil)
	}

	appLogger.Info("Initializing default logger: OK", nil)
}

