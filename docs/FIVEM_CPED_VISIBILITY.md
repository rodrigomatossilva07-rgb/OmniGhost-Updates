# Visibilidade FiveM por CPed (DMA)

O ESP lê um byte em `CPed + ped_visible_flag` por scatter na acquisition thread (~16–24 ms entre ciclos). O resultado, o valor cru e o instante da leitura ficam no `PedData` de cada ped; o ESP também os copia para o snapshot preparado. O render e o aimbot não efetuam uma leitura DMA adicional para visibilidade.

| Build | `ped_visible_flag` |
| --- | --- |
| 2802 | `0x147C` |
| 2944 | `0x147C` |
| 3095 | `0x147C` |
| 3258 | `0x147C` |
| 3751 | `0x147C` |
| 3788 | `0x147C` |

A tabela vem de `data/fivem_offsets.json`. A regra **provisória** é `0`, `4` ou `36` → oculto; qualquer outro byte lido → visível. Uma leitura falhada (`0xFF` usado como sentinela), offset ausente ou amostra com mais de 80 ms → desconhecido. O filtro `visible_check` rejeita o desconhecido. As cores de visibilidade só substituem as cores manuais quando a opção está ativa e existe uma amostra conhecida.

## Teste rápido em jogo (aproximadamente 2 minutos)

1. Com DMA ligado e build suportada, ativar ESP, caixa e **Cores por visibilidade**. Colocar um ped atrás de uma parede e outro em campo aberto. Confirmar cores independentes (oculto/visível).
2. Desativar **Cores por visibilidade** e confirmar a cor manual configurada para a caixa. Reativar e mover os peds para trocar a oclusão.
3. Ativar **Verificação de visibilidade** no ESP e no aim separadamente. Os peds ocultos e os de amostra desconhecida não devem ser desenhados/selecionados. Entrar e sair do servidor para verificar que não há visibilidade antiga nem crash.
4. Repetir por build e comparar os bytes crus de `PedData.visibility_flag` com estados observados. Medir FPS com a opção desligada e ligada. Se a regra falhar, corrigir o offset e/ou a interpretação antes da release.

**Limitação:** o byte do CPed é uma heurística de memória, não um raycast geométrico nem prova de LOS. O offset e o mapeamento ainda não foram confirmados numa sessão real com hardware DMA neste checkout.
