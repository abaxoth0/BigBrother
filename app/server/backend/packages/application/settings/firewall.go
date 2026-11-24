package settingsapplication

import "fmt"

const FirewallPublicEnabled = "firewall_public_enabled"

func PublicFirewallEnabled(settings QueryHandler) (bool, error) {
	value, err := settings.GetSetting(FirewallPublicEnabled)
	if err != nil {
		return false, err
	}
	switch value {
	case "", "1":
		return true, nil
	case "0":
		return false, nil
	default:
		return false, fmt.Errorf("invalid public firewall preference %q", value)
	}
}
