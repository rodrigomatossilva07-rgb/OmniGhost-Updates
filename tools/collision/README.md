# Pipeline de colisão do OmniGhost

O lançador único [`Atualizar-Colisao.bat`](../../Atualizar-Colisao.bat) atualiza os dados geométricos usados
pelos raycasts de visibilidade. Os ficheiros temporários ficam em `.cache/collision-build`; nada é espalhado
pela raiz do repositório.

## Organização

- `tools/collision/Update-CollisionData.ps1`: orquestra CS2 e FiveM.
- `tools/collision/extractors/cs2/`: localização portátil reservada ao código-fonte do extrator CS2.
- `tools/FiveMCollisionBuilder/`: conversor RageKit/YBN/YMAP/MLO para regiões `.ogcol`.
- `src/games/Cs2/collision/*.tri`: destino utilizado pelo runtime CS2.
- `data/fivem/collision/*.ogcol`: pacote regional externo utilizado pelo runtime FiveM.

O script também reconhece temporariamente o extrator existente em
`C:\Users\Rodrigo Silva\Downloads\CS2 EXTRATOR MAPAS`. Para tornar o repositório portátil, copia o projeto
licenciado para `tools/collision/extractors/cs2/`, preservando o respetivo `LICENSE`; não copies `bin`, `obj`,
`.vs` nem resultados antigos.

## Utilização

Abre `Atualizar-Colisao.bat` e escolhe o jogo. Também pode ser automatizado:

```powershell
& .\tools\collision\Update-CollisionData.ps1 -Game CS2 -Cs2Scope Official
& .\tools\collision\Update-CollisionData.ps1 -Game FiveM
& .\tools\collision\Update-CollisionData.ps1 -Game All
```

O FiveM requer RageKit compilável e as chaves locais em `%USERPROFILE%\.ragekit\keys`. Essas chaves nunca
devem ser copiadas para o projeto. A construção completa pode demorar e ocupar vários GiB lógicos antes da
compressão transparente do Windows.

## Distribuição

Dados de colisão não devem crescer indefinidamente dentro do executável:

- o CS2 possui um conjunto pequeno e finito de mapas oficiais e o pipeline atual comprime os `.tri` ao
  gerar os recursos incorporados;
- o FiveM possui mapa base, interiores e recursos personalizados, portanto usa um pacote externo regional,
  carregado sob procura por uma cache LRU;
- jogos futuros devem preferir pacotes externos versionados por jogo/build/mapa, com manifesto, tamanho e
  SHA-256, instalados apenas quando a funcionalidade de visibilidade for usada.

Nem todos os jogos precisam de mapas. Se o próprio jogo fornecer um estado de visibilidade fiável, ele deve
ser preferido. A geometria offline é necessária quando a decisão depende de raycast e não existe uma fonte
validada equivalente; mesmo assim, objetos dinâmicos exigem dados adicionais.
