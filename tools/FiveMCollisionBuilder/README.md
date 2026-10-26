# FiveM Collision Builder

Ferramenta offline que converte a colisão da instalação local do GTA V para o formato regional do OmniGhost.
Não lê memória do jogo e não é necessária durante a execução normal do cliente.

## Conteúdo exportado

- Colisão estática selecionada pela ordem real de carregamento do GTA V.
- Colisões `hi@` (podem ser desativadas).
- Colisões de interiores MLO aplicadas a cada instância do mapa.
- Primitivas RAGE convertidas em triângulos pelo RageKit.
- Materiais `SEE_THROUGH` excluídos por omissão.
- Regiões com triângulos, índices e BVH pré-construído (`.ogcol`).
- Validação estrutural imediata de cada região depois da gravação.
- `manifest.json` com versão do GTA, chave do índice e estatísticas.

Objetos dinâmicos, portas com estado variável, veículos e recursos personalizados de servidores FiveM não fazem
parte da geometria estática da instalação base.

## Compilar

```powershell
& "C:\Users\Rodrigo Silva\Downloads\OmniGhost\tools\FiveMCollisionBuilder\Build.ps1"
```

## Teste pequeno (Legion Square)

```powershell
$builder = "C:\Users\Rodrigo Silva\Downloads\OmniGhost\tools\FiveMCollisionBuilder\build\Release\FiveMCollisionBuilder.exe"
& $builder --output "C:\Users\Rodrigo Silva\Downloads\OmniGhost\data\fivem\collision-test" `
  --bounds 50 -980 380 -650 --region-size 256
```

## Mapa completo

```powershell
& $builder --output "C:\Users\Rodrigo Silva\Downloads\OmniGhost\data\fivem\collision" --region-size 512
```

O mapa completo pode produzir muitos dados e demorar. Valida primeiro uma área pequena. As chaves em
`%USERPROFILE%\.ragekit\keys` nunca devem ser copiadas para o projeto ou distribuídas.

## Otimizar armazenamento como os mapas do CS2

Depois de gerar o mapa completo, aplica compressão LZX transparente. O conteúdo e o formato `.ogcol`
continuam iguais; o Windows lê e descomprime apenas os blocos necessários, por isso a cache regional e
o raycast não precisam de extrair ficheiros temporários:

```powershell
& "C:\Users\Rodrigo Silva\Downloads\OmniGhost\tools\FiveMCollisionBuilder\Optimize-CollisionData.ps1"
```

Para remover a compressão mais tarde, executa o mesmo comando com `-Undo`.

## Formato `.ogcol` v1

Todos os inteiros são little-endian e todos os vetores usam `float32` nas coordenadas GTA V (X este, Y norte, Z cima):

1. magic de 8 bytes `OGCOL1\0\0`;
2. `uint32 version`;
3. `int32 cell_x`, `int32 cell_y`, `float region_size`;
4. `uint32 triangle_count`, `node_count`, `index_count`;
5. AABB global: dois `Vec3`;
6. triângulos: nove floats por entrada;
7. nós BVH: AABB e quatro `uint32` (`begin`, `count`, `left`, `right`);
8. índices de triângulos (`uint32`).

O runtime correspondente encontra-se em
`src/games/Fivem/collision/fivem_collision_world.{h,cpp}`. O carregador valida a assinatura, versão,
tamanho, limites, nós e índices antes de aceitar uma região; o raycast percorre diretamente o BVH
pré-construído, sem voltar a construir a árvore ao iniciar o OmniGhost.

