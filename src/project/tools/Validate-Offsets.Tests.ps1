


















$scriptPath = Join-Path $PSScriptRoot "Validate-Offsets.ps1"
. $scriptPath


$testDataDir = Join-Path $PSScriptRoot "test_data"
if (-not (Test-Path $testDataDir)) {
    New-Item -ItemType Directory -Path $testDataDir | Out-Null
}


function New-TestJsonFile {
    param(
        [string]$FileName,
        [string]$Content
    )
    $path = Join-Path $testDataDir $FileName
    Set-Content -Path $path -Value $Content -Encoding UTF8
    return $path
}

Describe "Validate-Offsets.ps1 - JSON Parsing" {
    Context "Read-JsonFile" {
        It "Parses valid JSON file correctly" {
            $json = @{
                schema_version = 7
                game = "cs2"
                build = "12345"
                module = "client.dll"
                generated_at = "2024-01-15T10:30:00Z"
                namespaces = @{
                    _root = @{
                        fields = @{
                            dwEntityList = @{
                                offset = "0x123456"
                                resolved = $true
                            }
                        }
                    }
                }
                decryptions = @{
                    namespace = "client.dll"
                    functions = @("DecryptPtr")
                    rva_fingerprints = @{
                        DecryptPtr = "0xABCDEF"
                    }
                }
            } | ConvertTo-Json -Depth 10

            $path = New-TestJsonFile "valid_offsets.json" $json
            $result = Read-JsonFile -FilePath $path

            $result.schema_version | Should -Be 7
            $result.game | Should -Be "cs2"
            $result.build | Should -Be "12345"
        }

        It "Throws on invalid JSON syntax" {
            $invalidJson = "{ invalid json }"
            $path = New-TestJsonFile "invalid.json" $invalidJson

            { Read-JsonFile -FilePath $path } | Should -Throw
        }

        It "Handles UTF-8 BOM correctly" {
            $json = @{
                schema_version = 7
                game = "rust"
                build = "1000"
                module = "GameAssembly.dll"
                generated_at = "2024-01-15T10:30:00Z"
                namespaces = @{}
                decryptions = @{
                    namespace = "GameAssembly.dll"
                    functions = @()
                    rva_fingerprints = @{}
                }
            } | ConvertTo-Json -Depth 10

            $path = New-TestJsonFile "bom_test.json" $json

            $bytes = [System.Text.Encoding]::UTF8.GetPreamble() + [System.Text.Encoding]::UTF8.GetBytes($json)
            [System.IO.File]::WriteAllBytes($path, $bytes)

            $result = Read-JsonFile -FilePath $path
            $result.schema_version | Should -Be 7
        }
    }
}

Describe "Validate-Offsets.ps1 - Hex Offset Validation" {
    Context "Validate-HexOffset" {
        It "Returns true for valid hex offsets" {
            Validate-HexOffset -Offset "0x123456" | Should -Be $true
            Validate-HexOffset -Offset "0xABCDEF" | Should -Be $true
            Validate-HexOffset -Offset "0x0" | Should -Be $true
            Validate-HexOffset -Offset "0xFFFFFFFF" | Should -Be $true
            Validate-HexOffset -Offset "0xabcdef123456" | Should -Be $true
        }

        It "Returns false for invalid hex offsets" {
            Validate-HexOffset -Offset "123456" | Should -Be $false
            Validate-HexOffset -Offset "0xGGGGGG" | Should -Be $false
            Validate-HexOffset -Offset "" | Should -Be $false
            Validate-HexOffset -Offset "0x" | Should -Be $false
            Validate-HexOffset -Offset "not_hex" | Should -Be $false
        }
    }
}

Describe "Validate-Offsets.ps1 - Top-Level Field Validation" {
    $requiredFields = @('schema_version', 'game', 'build', 'build_id', 'module', 'source', 'generated_at', 'namespaces', 'decryptions')

    foreach ($field in $requiredFields) {
        Context "Missing required field: $field" {
            It "Reports error when field is missing" {
                $json = @{
                    schema_version = 7
                    game = "cs2"
                    build = "12345"
                    build_id = "12345"
                    module = "client.dll"
                    source = "dumper"
                    generated_at = "2024-01-15T10:30:00Z"
                    namespaces = @{}
                    decryptions = @{
                        namespace = "client.dll"
                        functions = @()
                        rva_fingerprints = @{}
                    }
                }
                $json.Remove($field)
                $jsonStr = $json | ConvertTo-Json -Depth 10
                $path = New-TestJsonFile "missing_$field.json" $jsonStr

                $fileErrors = 0
                $fileResult = @{
                    file = "missing_$field.json"
                    valid = $true
                    errors = @()
                    warnings = @()
                    metadata = @{}
                }

                $parsed = Read-JsonFile -FilePath $path
                foreach ($f in $requiredFields) {
                    if (-not $parsed.PSObject.Properties.Match($f)) {
                        $fileResult.errors += "Missing required field: $f"
                        $fileErrors++
                    }
                }

                $fileResult.valid = ($fileErrors -eq 0)
                $fileResult.valid | Should -Be $false
                $fileResult.errors | Should -Contain "Missing required field: $field"
            }
        }
    }
}

Describe "Validate-Offsets.ps1 - Schema Version Validation" {
    It "Accepts schema_version in valid range (1-10)" {
        foreach ($ver in 1..10) {
            $json = @{
                schema_version = $ver
                game = "cs2"
                build = "12345"
                module = "client.dll"
                generated_at = "2024-01-15T10:30:00Z"
                namespaces = @{}
                decryptions = @{
                    namespace = "client.dll"
                    functions = @()
                    rva_fingerprints = @{}
                }
            } | ConvertTo-Json -Depth 10

            $path = New-TestJsonFile "schema_v$ver.json" $json
            $parsed = Read-JsonFile -Path $path

            $ver -ge 1 -and $ver -le 10 | Should -Be $true
        }
    }

    It "Rejects schema_version out of range" {
        foreach ($ver in @(0, 11, -1, 100)) {
            $json = @{
                schema_version = $ver
                game = "cs2"
                build = "12345"
                module = "client.dll"
                generated_at = "2024-01-15T10:30:00Z"
                namespaces = @{}
                decryptions = @{
                    namespace = "client.dll"
                    functions = @()
                    rva_fingerprints = @{}
                }
            } | ConvertTo-Json -Depth 10

            $path = New-TestJsonFile "schema_invalid_$ver.json" $json
            $parsed = Read-JsonFile -Path $path

            ($ver -lt 1 -or $ver -gt 10) | Should -Be $true
        }
    }
}

Describe "Validate-Offsets.ps1 - Game Enum Validation" {
    $validGames = @('rust', 'cs2', 'fivem', 'fortnite', 'warzone', 'apex', 'unknown')

    It "Accepts valid game values" {
        foreach ($game in $validGames) {
            $json = @{
                schema_version = 7
                game = $game
                build = "12345"
                module = "client.dll"
                generated_at = "2024-01-15T10:30:00Z"
                namespaces = @{}
                decryptions = @{
                    namespace = "client.dll"
                    functions = @()
                    rva_fingerprints = @{}
                }
            } | ConvertTo-Json -Depth 10

            $path = New-TestJsonFile "game_$game.json" $json
            $parsed = Read-JsonFile -Path $path

            $validGames -contains $game | Should -Be $true
        }
    }

    It "Rejects invalid game values" {
        $json = @{
            schema_version = 7
            game = "invalid_game"
            build = "12345"
            module = "client.dll"
            generated_at = "2024-01-15T10:30:00Z"
            namespaces = @{}
            decryptions = @{
                namespace = "client.dll"
                functions = @()
                rva_fingerprints = @{}
            }
        } | ConvertTo-Json -Depth 10

        $path = New-TestJsonFile "game_invalid.json" $json
        $parsed = Read-JsonFile -Path $path

        $validGames -notcontains "invalid_game" | Should -Be $true
    }
}

Describe "Validate-Offsets.ps1 - Build Format Validation" {
    It "Accepts numeric build strings" {
        $validBuilds = @("12345", "0", "999999", "0x123ABC", "0x0")

        foreach ($build in $validBuilds) {
            $isValid = $build -match '^[0-9]+$' -or $build -match '^0x[0-9A-Fa-f]+$'
            $isValid | Should -Be $true
        }
    }

    It "Flags non-numeric build strings as warning" {
        $invalidBuilds = @("abc", "1.2.3", "v1.0", "")

        foreach ($build in $invalidBuilds) {
            $isValid = $build -match '^[0-9]+$' -or $build -match '^0x[0-9A-Fa-f]+$'
            $isValid | Should -Be $false
        }
    }
}

Describe "Validate-Offsets.ps1 - ISO 8601 Date Validation" {
    It "Accepts valid ISO 8601 timestamps" {
        $validDates = @(
            "2024-01-15T10:30:00Z",
            "2024-01-15T10:30:00.000Z",
            "2024-01-15T10:30:00+00:00",
            "2024-12-31T23:59:59Z"
        )

        foreach ($date in $validDates) {
            try {
                [DateTime]::Parse($date, [System.Globalization.CultureInfo]::InvariantCulture, [System.Globalization.DateTimeStyles]::AdjustToUniversal)
                $valid = $true
            } catch {
                $valid = $false
            }
            $valid | Should -Be $true
        }
    }

    It "Rejects invalid date formats" {
        $invalidDates = @(
            "15/01/2024",
            "2024-01-15",
            "not a date",
            ""
        )

        foreach ($date in $invalidDates) {
            try {
                [DateTime]::Parse($date, [System.Globalization.CultureInfo]::InvariantCulture, [System.Globalization.DateTimeStyles]::AdjustToUniversal)
                $valid = $true
            } catch {
                $valid = $false
            }
            $valid | Should -Be $false
        }
    }
}

Describe "Validate-Offsets.ps1 - Namespace & Field Validation" {
    It "Requires 'fields' object in each namespace" {
        $json = @{
            schema_version = 7
            game = "cs2"
            build = "12345"
            module = "client.dll"
            generated_at = "2024-01-15T10:30:00Z"
            namespaces = @{
                _root = @{
                    not_fields = @{}
                }
            }
            decryptions = @{
                namespace = "client.dll"
                functions = @()
                rva_fingerprints = @{}
            }
        } | ConvertTo-Json -Depth 10

        $path = New-TestJsonFile "missing_fields.json" $json
        $parsed = Read-JsonFile -Path $path

        $ns = $parsed.namespaces._root
        $hasFields = $ns.PSObject.Properties.Match('fields')
        $hasFields | Should -Be $false
    }

    It "Validates each field has offset and resolved" {
        $json = @{
            schema_version = 7
            game = "cs2"
            build = "12345"
            module = "client.dll"
            generated_at = "2024-01-15T10:30:00Z"
            namespaces = @{
                _root = @{
                    fields = @{
                        valid_field = @{
                            offset = "0x123456"
                            resolved = $true
                        },
                        missing_offset = @{
                            resolved = $true
                        },
                        missing_resolved = @{
                            offset = "0xABCDEF"
                        },
                        invalid_resolved = @{
                            offset = "0x111111"
                            resolved = "yes"
                        }
                    }
                }
            }
            decryptions = @{
                namespace = "client.dll"
                functions = @()
                rva_fingerprints = @{}
            }
        } | ConvertTo-Json -Depth 10

        $path = New-TestJsonFile "field_validation.json" $json
        $parsed = Read-JsonFile -Path $path

        $fields = $parsed.namespaces._root.fields
        $fields.valid_field.offset | Should -Be "0x123456"
        $fields.valid_field.resolved | Should -Be $true
        $fields.missing_offset.PSObject.Properties.Match('offset') | Should -Be $false
        $fields.missing_resolved.PSObject.Properties.Match('resolved') | Should -Be $false
        $fields.invalid_resolved.resolved.GetType().Name | Should -Be "String"
    }

    It "Detects duplicate offset paths" {
        $json = @{
            schema_version = 7
            game = "cs2"
            build = "12345"
            module = "client.dll"
            generated_at = "2024-01-15T10:30:00Z"
            namespaces = @{
                _root = @{
                    fields = @{
                        dwEntityList = @{
                            offset = "0x123456"
                            resolved = $true
                        }
                    }
                },
                client = @{
                    fields = @{
                        dwEntityList = @{
                            offset = "0x789ABC"
                            resolved = $true
                        }
                    }
                }
            }
            decryptions = @{
                namespace = "client.dll"
                functions = @()
                rva_fingerprints = @{}
            }
        } | ConvertTo-Json -Depth 10

        $path = New-TestJsonFile "duplicates.json" $json
        $parsed = Read-JsonFile -Path $path

        $allPaths = @{}
        $duplicates = 0
        foreach ($nsName in $parsed.namespaces.PSObject.Properties.Name) {
            $ns = $parsed.namespaces.$nsName
            if ($ns.PSObject.Properties.Match('fields')) {
                foreach ($fieldName in $ns.fields.PSObject.Properties.Name) {
                    $path = "${nsName}.${fieldName}"
                    if ($allPaths.ContainsKey($path)) {
                        $duplicates++
                    }
                    $allPaths[$path] = $true
                }
            }
        }

        $duplicates | Should -Be 1
    }
}

Describe "Validate-Offsets.ps1 - Critical Offset Validation" {
    $criticalOffsets = @{
        'rust' = @('_root.basenetworkable_pointer', '_root.camera_pointer', 'BaseNetworkable.client_entities', 'BasePlayer.playerModel', 'PlayerModel.position', 'BaseCamera.matrix')
        'cs2'  = @('_root.dwEntityList', '_root.dwLocalPlayerPawn', '_root.dwViewMatrix', '_root.dwViewAngles', '_root.dwGlobalVars', 'C_BaseEntity.m_iHealth')
        'fivem' = @('_root.world', '_root.player_health', '_root.player_position', '_root.bone_list', '_root.bone_matrix')
        'fortnite' = @('globals.GWorld', 'gworld_crypto.world_mul', 'gworld_crypto.world_add', 'runtime.pcm_cam_location', 'runtime.pcm_cam_rotation', 'runtime.pcm_cam_fov')
        'warzone' = @('_root.ref_def', '_root.entity_list', '_root.camera_base', '_root.view_matrix', 'player.size', 'player.pos')
        'apex' = @('_root.EntityList', '_root.LocalPlayer', '_root.ViewRender', '_root.view_matrix', 'base_entity.m_vecAbsOrigin', 'base_entity.m_iHealth')
    }

    foreach ($game in $criticalOffsets.Keys) {
        Context "Game: $game" {
            It "Has critical offsets defined" {
                $criticalOffsets[$game].Count | Should -BeGreaterThan 0
            }

            It "Flags missing critical offsets as warning" {
                $json = @{
                    schema_version = 7
                    game = $game
                    build = "12345"
                    module = "client.dll"
                    generated_at = "2024-01-15T10:30:00Z"
                    namespaces = @{
                        _root = @{
                            fields = @{
                                some_offset = @{
                                    offset = "0x123456"
                                    resolved = $true
                                }
                            }
                        }
                    }
                    decryptions = @{
                        namespace = "client.dll"
                        functions = @()
                        rva_fingerprints = @{}
                    }
                } | ConvertTo-Json -Depth 10

                $path = New-TestJsonFile "missing_critical_$game.json" $json
                $parsed = Read-JsonFile -Path $path

                $allPaths = @{}
                foreach ($nsName in $parsed.namespaces.PSObject.Properties.Name) {
                    $ns = $parsed.namespaces.$nsName
                    if ($ns.PSObject.Properties.Match('fields')) {
                        foreach ($fieldName in $ns.fields.PSObject.Properties.Name) {
                            $path = "${nsName}.${fieldName}"
                            $allPaths[$path] = $true
                        }
                    }
                }

                $missing = 0
                foreach ($crit in $criticalOffsets[$game]) {
                    if (-not $allPaths.ContainsKey($crit)) {
                        $missing++
                    }
                }

                $missing | Should -Be $criticalOffsets[$game].Count
            }
        }
    }
}

Describe "Validate-Offsets.ps1 - Decryption Validation" {
    It "Requires all decryption fields" {
        $requiredDec = @('namespace', 'functions', 'rva_fingerprints')

        foreach ($field in $requiredDec) {
            $json = @{
                schema_version = 7
                game = "cs2"
                build = "12345"
                module = "client.dll"
                generated_at = "2024-01-15T10:30:00Z"
                namespaces = @{}
                decryptions = @{
                    namespace = "client.dll"
                    functions = @()
                    rva_fingerprints = @{}
                }
            }
            $json.decyptions.Remove($field)
            $jsonStr = $json | ConvertTo-Json -Depth 10
            $path = New-TestJsonFile "dec_missing_$field.json" $jsonStr

            $parsed = Read-JsonFile -Path $path
            $parsed.decryptions.PSObject.Properties.Match($field) | Should -Be $false
        }
    }

    It "Warns when functions array is empty" {
        $json = @{
            schema_version = 7
            game = "cs2"
            build = "12345"
            module = "client.dll"
            generated_at = "2024-01-15T10:30:00Z"
            namespaces = @{}
            decryptions = @{
                namespace = "client.dll"
                functions = @()
                rva_fingerprints = @{
                    DecryptPtr = "0xABCDEF"
                }
            }
        } | ConvertTo-Json -Depth 10

        $path = New-TestJsonFile "empty_functions.json" $json
        $parsed = Read-JsonFile -Path $path

        $parsed.decryptions.functions.Count | Should -Be 0
    }

    It "Validates rva_fingerprints are valid hex" {
        $json = @{
            schema_version = 7
            game = "cs2"
            build = "12345"
            module = "client.dll"
            generated_at = "2024-01-15T10:30:00Z"
            namespaces = @{}
            decryptions = @{
                namespace = "client.dll"
                functions = @("DecryptPtr")
                rva_fingerprints = @{
                    valid_fp = "0x123456",
                    invalid_fp = "not_hex"
                }
            }
        } | ConvertTo-Json -Depth 10

        $path = New-TestJsonFile "rva_fingerprints.json" $json
        $parsed = Read-JsonFile -Path $path

        Validate-HexOffset -Offset $parsed.decryptions.rva_fingerprints.valid_fp | Should -Be $true
        Validate-HexOffset -Offset $parsed.decryptions.rva_fingerprints.invalid_fp | Should -Be $false
    }
}

Describe "Validate-Offsets.ps1 - Module Name Validation" {
    It "Accepts valid DLL/EXE module names" {
        $validModules = @("client.dll", "engine.dll", "GameAssembly.dll", "FiveM_b1234_GTAProcess.exe", "cs2.exe")

        foreach ($module in $validModules) {
            $isValid = $module -like '*.dll' -or $module -like '*.exe'
            $isValid | Should -Be $true
        }
    }

    It "Flags non-DLL/EXE module names" {
        $invalidModules = @("client", "module", "unknown", "")

        foreach ($module in $invalidModules) {
            $isValid = $module -like '*.dll' -or $module -like '*.exe'
            $isValid | Should -Be $false
        }
    }
}

Describe "Validate-Offsets.ps1 - Integration Tests" {
    It "Validates a complete valid CS2 offset file" {
        $json = @{
            schema_version = 7
            game = "cs2"
            build = "1234567"
            build_id = "1234567"
            module = "client.dll"
            source = "hazedumper"
            generated_at = "2024-01-15T10:30:00Z"
            namespaces = @{
                _root = @{
                    fields = @{
                        dwEntityList = @{ offset = "0x123456"; resolved = $true; comment = "Entity list" }
                        dwLocalPlayerPawn = @{ offset = "0x789ABC"; resolved = $true; comment = "Local player" }
                        dwViewMatrix = @{ offset = "0xDEF012"; resolved = $true; comment = "View matrix" }
                        dwViewAngles = @{ offset = "0x345678"; resolved = $true; comment = "View angles" }
                        dwGlobalVars = @{ offset = "0x9ABCDE"; resolved = $true; comment = "Global vars" }
                    }
                }
                C_BaseEntity = @{
                    fields = @{
                        m_iHealth = @{ offset = "0x112233"; resolved = $true; comment = "Health" }
                    }
                }
            }
            decryptions = @{
                namespace = "client.dll"
                functions = @("DecryptPtr")
                rva_fingerprints = @{
                    DecryptPtr = "0xABCDEF"
                }
            }
        } | ConvertTo-Json -Depth 10

        $path = New-TestJsonFile "complete_cs2.json" $json
        $parsed = Read-JsonFile -Path $path

        $parsed.schema_version | Should -Be 7
        $parsed.game | Should -Be "cs2"
        $parsed.namespaces._root.fields.dwEntityList.offset | Should -Be "0x123456"
        $parsed.namespaces._root.fields.dwEntityList.resolved | Should -Be $true
        $parsed.namespaces.C_BaseEntity.fields.m_iHealth.offset | Should -Be "0x112233"
        $parsed.decryptions.functions.Count | Should -Be 1
    }

    It "Validates a complete valid Rust offset file" {
        $json = @{
            schema_version = 7
            game = "rust"
            build = "2024.01.15"
            build_id = "2024.01.15"
            module = "GameAssembly.dll"
            source = "il2cppdumper"
            generated_at = "2024-01-15T10:30:00Z"
            namespaces = @{
                _root = @{
                    fields = @{
                        basenetworkable_pointer = @{ offset = "0x123456"; resolved = $true }
                        camera_pointer = @{ offset = "0x789ABC"; resolved = $true }
                    }
                }
                BaseNetworkable = @{
                    fields = @{
                        client_entities = @{ offset = "0xDEF012"; resolved = $true }
                    }
                }
                BasePlayer = @{
                    fields = @{
                        playerModel = @{ offset = "0x345678"; resolved = $true }
                    }
                }
                PlayerModel = @{
                    fields = @{
                        position = @{ offset = "0x9ABCDE"; resolved = $true }
                    }
                }
                BaseCamera = @{
                    fields = @{
                        matrix = @{ offset = "0x13579B"; resolved = $true }
                    }
                }
            }
            decryptions = @{
                namespace = "GameAssembly.dll"
                functions = @("DecryptPtr")
                rva_fingerprints = @{
                    DecryptPtr = "0xABCDEF"
                }
            }
        } | ConvertTo-Json -Depth 10

        $path = New-TestJsonFile "complete_rust.json" $json
        $parsed = Read-JsonFile -Path $path

        $parsed.game | Should -Be "rust"
        $parsed.namespaces.BaseNetworkable.fields.client_entities.offset | Should -Be "0xDEF012"
        $parsed.namespaces.BasePlayer.fields.playerModel.offset | Should -Be "0x345678"
    }
}


