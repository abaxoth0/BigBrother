package firewall

import (
	"fmt"
	"strings"
)

const TCPRule = "BigBrother Server (TCP)"
const DiscoveryRule = "BigBrother Discovery (UDP 42069)"
const LegacyTCPRule = "BigBrother Server (TCP 1984)"

func syncRules(program string, tcpPort, discoveryPort int, run func(string) error) error {
	if program == "" || tcpPort < 1 || tcpPort > 65535 || discoveryPort < 1 || discoveryPort > 65535 {
		return fmt.Errorf("invalid firewall program or port")
	}
	// PowerShell single-quoted literals escape quotes by doubling them.
	program = strings.ReplaceAll(program, "'", "''")
	script := fmt.Sprintf(`$ErrorActionPreference = 'Stop'
try {
 $names = @('%s', '%s', '%s')
 Get-NetFirewallRule -PolicyStore PersistentStore | Where-Object { $_.DisplayName -in $names } | Remove-NetFirewallRule
 New-NetFirewallRule -PolicyStore PersistentStore -DisplayName '%s' -Direction Inbound -Action Allow -Enabled True -Profile Private,Domain -Program '%s' -Protocol TCP -LocalPort %d | Out-Null
 New-NetFirewallRule -PolicyStore PersistentStore -DisplayName '%s' -Direction Inbound -Action Allow -Enabled True -Profile Private,Domain -Program '%s' -Protocol UDP -LocalPort %d -RemoteAddress LocalSubnet | Out-Null
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
`, TCPRule, LegacyTCPRule, DiscoveryRule, TCPRule, program, tcpPort, DiscoveryRule, program, discoveryPort)
	if err := run(script); err != nil {
		return fmt.Errorf("synchronize Windows Firewall rules: %w", err)
	}
	return nil
}
