# Código-fonte do extrator CS2

Coloca aqui apenas os ficheiros-fonte do projeto `PhysExtractor` e a sua licença:

- `PhysExtractor.csproj`
- `src/Program.cs`
- `src/kv3parser.cs`
- `LICENSE`

O pipeline também aceita outra pasta através de `-Cs2ExtractorRoot`. Não são necessários `bin/`, `obj/`,
`.vs/` nem mapas gerados, porque a compilação e a extração decorrem em `.cache/collision-build`.
