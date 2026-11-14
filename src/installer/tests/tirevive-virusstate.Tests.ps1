# Pester 5 tests for tirevive-virusstate.ps1 (issue #31: ask to unplug a Virus on the Access driver, wait for the
# re-detection before the "now:" check). Run: native ctest (installer_scripts, needs pwsh + Pester 5) or
# Invoke-Pester src/installer/tests.
BeforeAll {
    . (Join-Path $PSScriptRoot '..' 'tirevive-virusstate.ps1')
    function Dev($id, $present, $svc) { [pscustomobject]@{ InstanceId = $id; Present = $present; Service = $svc } }
    $parent = 'USB\VID_133E&PID_0815\5&18F54CB7&0&2'
    $mi03 = 'USB\VID_133E&PID_0815&MI_03\6&735A86F&2&0003'
}

Describe 'Get-AccessBoundVirus' {
    It 'finds a connected Virus on VIRUSUSB and its MIDI interface on vtimidi (#31)' {
        $found = Get-AccessBoundVirus @((Dev $parent $true 'VIRUSUSB'), (Dev $mi03 $true 'vtimidi'))
        $found.Count | Should -Be 2
    }

    It 'ignores a Virus on TI Revive''s drivers' {
        (Get-AccessBoundVirus @((Dev $parent $true 'usbccgp'), (Dev $mi03 $true 'WINUSB'))).Count | Should -Be 0
    }

    It 'ignores leftover nodes of an unplugged Virus' {
        (Get-AccessBoundVirus @((Dev $parent $false 'VIRUSUSB'))).Count | Should -Be 0
    }

    It 'ignores other vendors and nodes without a driver' {
        (Get-AccessBoundVirus @((Dev 'USB\VID_1234&PID_0815\1' $true 'VIRUSUSB'), (Dev $parent $true $null))).Count | Should -Be 0
    }
}

Describe 'Test-VirusDetected' {
    It 'is true once the device node is present with a driver' {
        Test-VirusDetected @((Dev $parent $true 'usbccgp')) | Should -BeTrue
        Test-VirusDetected @((Dev $parent $true 'VIRUSUSB')) | Should -BeTrue
    }

    It 'is false while only an interface or a driverless node is there (still being detected, #31 run 2)' {
        Test-VirusDetected @((Dev $mi03 $true 'WINUSB')) | Should -BeFalse
        Test-VirusDetected @((Dev $parent $true $null)) | Should -BeFalse
        Test-VirusDetected @() | Should -BeFalse
    }
}

Describe 'Wait-Until' {
    It 'returns true as soon as the condition holds' {
        $script:n = 0
        Wait-Until { $script:n++; $script:n -ge 3 } 5 0.01 | Should -BeTrue
        $script:n | Should -Be 3
    }

    It 'returns false after the timeout' {
        Wait-Until { $false } 0 0.01 | Should -BeFalse
    }
}
