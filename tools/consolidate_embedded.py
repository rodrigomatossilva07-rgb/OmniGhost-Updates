import re

with open('C:\\Users\\Rodrigo\\Downloads\\OmniGhost\\OmiGhost.vcxproj', 'r', encoding='utf-8') as f:
    content = f.read()

# Find and replace the three embedded generation targets with a single consolidated target
old_targets = '''  <Target Name="BuildOmniGhostEmbeddedOffsets" BeforeTargets="ResourceCompile" 
Condition="\'$(DesignTimeBuild)\'!=\'true\'">
      <Message Importance="High" Text="[OmniGhost Offsets] Generating embedded offset resources..."/>
      <Exec Command=""$(MSBuildProjectDirectory)\\tools\\Run-OmniGhostPowerShell.cmd" 
generate-embedded-offs "$(MSBuildProjectDirectory)\" "$(Configuration)\" "$(Platform)\"" IgnoreExitCode="true">
        <Output TaskParameter="ExitCode" PropertyName="OmniGhostEmbeddedOffsetsExitCode"/>
      </Exec>
      <Error Condition="\'$(OmniGhostEmbeddedOffsetsExitCode)\'!=\'0\'" Code="OGOFF001" Text="Falha ao gerar os recursos 
de offsets embutidos. Consulta a janela Sa�da."/>
    </Target>
  <Target Name="BuildOmniGhostEmbeddedRuntime" BeforeTargets="ClCompile;ResourceCompile" 
Condition="\'$(DesignTimeBuild)\'!=\'true\'">
      <Message Importance="High" Text="[OmniGhost Runtime] Generating single-process native dependency bundle..."/>
      <Exec Command=""$(MSBuildProjectDirectory)\\tools\\Run-OmniGhostPowerShell.cmd" 
generate-embedded-runtime "$(MSBuildProjectDirectory)\" 
"$(VCInstallDir)Redist\\MSVC\\$(VCToolsVersion)\\x64" "$(Configuration)\" "$(Platform)\"" IgnoreExitCode="true">
        <Output TaskParameter="ExitCode" PropertyName="OmniGhostEmbeddedRuntimeExitCode"/>
      </Exec>
      <Error Condition="\'$(OmniGhostEmbeddedRuntimeExitCode)\'!=\'0\'" Code="OGRUN001" Text="Falha ao gerar o runtime 
auto-instalável. Consulta a janela Saída."/>
    </Target>
  <Target Name="BuildOmniGhostEmbeddedResources" BeforeTargets="ClCompile;ResourceCompile" 
Condition="\'$(DesignTimeBuild)\'!=\'true\'">
      <Message Importance="High" Text="[OmniGhost Resources] Validating and generating embedded runtime resources..."/>
      <Exec Command=""$(MSBuildProjectDirectory)\\tools\\Run-OmniGhostPowerShell.cmd" 
generate-embedded-resources "$(MSBuildProjectDirectory)\" "$(Configuration)\" "$(Platform)\"" IgnoreExitCode="true">
        <Output TaskParameter="ExitCode" PropertyName="OmniGhostEmbeddedResourcesExitCode"/>
      </Exec>
      <Error Condition="\'$(OmniGhostEmbeddedResourcesExitCode)\'!=\'0\'" Code="OGRES001" Text="Falha ao gerar os recursos 
embedded. Consulta a janela Saída."/>
    </Target>'''

new_target = '''  <Target Name="BuildOmniGhostEmbedded" BeforeTargets="ClCompile;ResourceCompile" 
Condition="'$(DesignTimeBuild)'!='true'">
      <Message Importance="High" Text="[OmniGhost] Generating all embedded resources (offsets, runtime, resources)..."/>
      <Exec Command=""$(MSBuildProjectDirectory)\\tools\\Run-OmniGhostPowerShell.cmd" 
generate-embedded-all "$(MSBuildProjectDirectory)\" "$(Configuration)\" "$(Platform)\"" IgnoreExitCode="true">
        <Output TaskParameter="ExitCode" PropertyName="OmniGhostEmbeddedExitCode"/>
      </Exec>
      <Error Condition="'$(OmniGhostEmbeddedExitCode)'!='0'" Code="OGEMB001" Text="Falha ao gerar recursos embutidos. Consulta a janela Saída."/>
    </Target>'''

content = content.replace(old_targets, new_target)

with open('C:\\Users\\Rodrigo\\Downloads\\OmniGhost\\OmiGhost.vcxproj', 'w', encoding='utf-8') as f:
    f.write(content)

print("Done")