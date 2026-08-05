<p align="center">
  <img
    width="100%"
    alt="OMNIGHOST"
    src="https://capsule-render.vercel.app/api?type=waving&height=230&color=0:080808,35:17130A,55:D4AF37,75:17130A,100:080808&text=OMNIGHOST&fontColor=FFFFFF&fontSize=52&fontAlignY=38&desc=DMA%20Launcher%20for%20Windows&descAlignY=59&descSize=17"
  />
</p>

<div align="center">

[![Windows](https://img.shields.io/badge/WINDOWS-x64-D4AF37?style=for-the-badge&logo=windows&logoColor=white&labelColor=101010)](#-requisitos)
[![DMA](https://img.shields.io/badge/DMA-REQUIRED-C9A227?style=for-the-badge&labelColor=101010)](#-requisitos)
[![MAKCU](https://img.shields.io/badge/MAKCU-OPTIONAL-8D7627?style=for-the-badge&labelColor=101010)](#-requisitos)
[![Fuser](https://img.shields.io/badge/FUSER-OPTIONAL-8D7627?style=for-the-badge&labelColor=101010)](#-requisitos)

<br>

### Biblioteca, configuração e experiência DMA num único launcher.

O **OMNIGHOST** centraliza hardware, módulos, configurações e atualizações  
numa interface moderna criada para Windows x64.

<br>

[![Latest Release](https://img.shields.io/github/v/release/rodrigomatossilva07-rgb/OmniGhost-Updates?display_name=tag&style=for-the-badge&label=LATEST%20RELEASE&labelColor=101010&color=D4AF37)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest)
[![Download](https://img.shields.io/badge/DOWNLOAD-OMNIGHOST.ZIP-C9A227?style=for-the-badge&logo=github&logoColor=white&labelColor=101010)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest)

</div>

---

## ⚠️ Aviso legal e utilização responsável

O **OMNIGHOST** é um projeto independente destinado exclusivamente a investigação, desenvolvimento e utilização em sistemas ou ambientes nos quais o utilizador possua autorização explícita.

O utilizador é responsável por cumprir:

- a legislação aplicável;
- os termos de utilização dos jogos;
- as regras das plataformas utilizadas;
- as políticas de serviços de terceiros;
- as normas do ambiente onde o software for executado.

> [!CAUTION]
> Não utilizes o OMNIGHOST em contas, sistemas, servidores ou ambientes onde este tipo de hardware ou software não seja autorizado. O projeto não concede permissão para contornar regras, medidas de segurança ou restrições impostas por terceiros.

> [!IMPORTANT]
> O OMNIGHOST necessita de uma **placa DMA compatível** para as funcionalidades dependentes de DMA.  
> O **MAKCU** e o **fuser** são componentes opcionais e dependem do setup utilizado.

Todos os nomes, marcas, logótipos e produtos mencionados pertencem aos respetivos proprietários. A referência a esses produtos não implica associação, aprovação, patrocínio ou parceria oficial.

---

## ✦ Índice

<table>
<tr>
<td width="50%">

- [Sobre o OMNIGHOST](#-sobre-o-omnighost)
- [Destaques](#-destaques)
- [Experiência do launcher](#-experiência-do-launcher)
- [Instalação](#-instalação)
- [Página Atualizações](#-página-atualizações)

</td>
<td width="50%">

- [Requisitos](#-requisitos)
- [Resolução de problemas](#-resolução-de-problemas)
- [Suporte](#-suporte)
- [Estado do projeto](#-estado-do-projeto)

</td>
</tr>
</table>

---

## ✦ Sobre o OMNIGHOST

O **OMNIGHOST** é um launcher para Windows desenvolvido para centralizar a experiência de um projeto baseado em hardware **DMA**.

A aplicação reúne numa única interface:

- biblioteca e integrações disponíveis;
- estado dos componentes do setup;
- configurações organizadas por módulo;
- ações de inicialização;
- histórico real de versões;
- verificação de novas atualizações;
- informação de compatibilidade do ambiente.

O design combina uma identidade visual escura com detalhes dourados, navegação compacta e estados claros para cada operação.

### Arquitetura do setup

```text
┌──────────────────────────────────────────────────────┐
│                    OMNIGHOST                         │
│        Biblioteca · Configuração · Atualizações      │
└──────────────────────────┬───────────────────────────┘
                           │
             ┌─────────────┴─────────────┐
             │                           │
      Placa DMA compatível         Componentes opcionais
          Obrigatória               MAKCU · Fuser
```

### Hardware principal

| Componente | Estado | Função no setup |
|:--|:--:|:--|
| **Placa DMA compatível** | **Obrigatória** | Componente principal das funcionalidades dependentes de DMA |
| **MAKCU** | Opcional | Integração adicional de entrada, quando suportada pelo setup |
| **Fuser** | Opcional | Combinação ou apresentação de fontes de imagem |
| **Ligação de rede** | Recomendada | Atualizações, histórico de versões e conteúdo remoto |

> [!NOTE]
> A compatibilidade depende do modelo da placa, firmware, drivers, cabos, portas utilizadas e configuração dos computadores envolvidos.

---

## ✦ Destaques

<table>
<tr>
<td width="50%" valign="top">

### ◈ Interface centralizada

Biblioteca, módulos, definições e estado do sistema reunidos numa única aplicação.

</td>
<td width="50%" valign="top">

### ◈ Integração DMA

Estrutura preparada para trabalhar com uma placa DMA compatível e diferentes configurações de hardware.

</td>
</tr>

<tr>
<td width="50%" valign="top">

### ◈ Hardware modular

O setup pode ser complementado com MAKCU e fuser sem os tornar obrigatórios para todas as instalações.

</td>
<td width="50%" valign="top">

### ◈ Atualizações reais

O histórico é baseado em versões efetivamente publicadas, sem dados fictícios apresentados como alterações reais.

</td>
</tr>

<tr>
<td width="50%" valign="top">

### ◈ Experiência consistente

Tema escuro, detalhes dourados, cartões compactos e organização por componente.

</td>
<td width="50%" valign="top">

### ◈ Verificação em segundo plano

O launcher pode procurar novas versões sem bloquear a abertura normal da interface.

</td>
</tr>

<tr>
<td width="50%" valign="top">

### ◈ Estados claros

Informação visual para carregamento, ausência de ligação, falhas temporárias e conteúdo em cache.

</td>
<td width="50%" valign="top">

### ◈ Desenvolvimento contínuo

Arquitetura preparada para evolução da interface, compatibilidade e integrações futuras.

</td>
</tr>
</table>

---

## ✦ Experiência do launcher

### ◇ Biblioteca

A Biblioteca apresenta os jogos e integrações disponíveis numa área centralizada.

Cada entrada pode disponibilizar informações como:

- estado do módulo;
- disponibilidade da instalação;
- compatibilidade;
- ações principais;
- configurações relacionadas;
- avisos relevantes para o setup.

### ◇ Configuração

As opções são separadas por componente para evitar misturar definições globais com configurações específicas.

Esta organização facilita:

- localizar opções;
- identificar o componente afetado;
- reduzir configurações incorretas;
- manter uma experiência consistente;
- preparar futuras integrações.

### ◇ Estado do hardware

O launcher pode apresentar informações relacionadas com a disponibilidade dos componentes necessários ao ambiente.

O objetivo é permitir confirmar, antes da utilização, se:

- a placa DMA está disponível;
- os componentes esperados foram reconhecidos;
- a configuração necessária foi carregada;
- existem avisos relacionados com o hardware;
- o setup está preparado.

### ◇ Navegação

A interface foi desenhada para oferecer:

<table>
<tr>
<td width="33%" align="center">

**Navegação rápida**

Transição simples entre Biblioteca, Atualizações e outras áreas.

</td>
<td width="33%" align="center">

**Conteúdo organizado**

Pesquisa, filtros, cartões e categorias consistentes.

</td>
<td width="33%" align="center">

**Identidade visual**

Tema escuro com detalhes dourados e estados discretos.

</td>
</tr>
</table>

---

## ✦ Instalação

### 01 — Descarregar

Abre a página da versão mais recente:

<div align="center">

[![Download Latest](https://img.shields.io/badge/DESCARREGAR-ÚLTIMA%20VERSÃO-D4AF37?style=for-the-badge&logo=github&logoColor=white&labelColor=101010)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest)

</div>

Na secção **Assets**, descarrega:

```text
OmniGhost.zip
```

> [!WARNING]
> Não utilizes `Source code (zip)` ou `Source code (tar.gz)` como pacote do launcher. Esses ficheiros são criados automaticamente pelo GitHub.

### 02 — Extrair

Extrai todo o conteúdo para uma pasta própria.

Exemplo:

```text
C:\OmniGhost\
```

A estrutura deverá incluir:

```text
C:\OmniGhost\
├── OmniGhost.exe
├── data\
├── fonts\
├── images\
└── restantes ficheiros necessários
```

### 03 — Preparar o hardware

Antes de iniciar o launcher, confirma:

- placa DMA instalada e ligada corretamente;
- drivers exigidos pelo fabricante disponíveis;
- firmware compatível com o equipamento;
- cabos e portas corretamente ligados;
- MAKCU ligado apenas quando fizer parte do setup;
- fuser configurado apenas quando necessário.

### 04 — Executar

Abre:

```text
OmniGhost.exe
```

> [!IMPORTANT]
> Não executes o programa diretamente dentro do ZIP. A aplicação deve ser extraída com todos os seus ficheiros.

> [!NOTE]
> A placa DMA é necessária para as funcionalidades DMA. O MAKCU e o fuser são opcionais.

---

## ✦ Página Atualizações

A página **Atualizações** apresenta o histórico real das versões publicadas do OMNIGHOST.

As entradas são organizadas por versão, componente e categoria.

### Categorias suportadas

| Categoria | Identificação | Descrição |
|:--|:--:|:--|
| **Adicionado** | `NEW` | Novas funcionalidades, módulos ou opções |
| **Melhorado** | `IMPROVED` | Melhorias de estabilidade, utilização ou interface |
| **Corrigido** | `FIXED` | Correções de problemas identificados |
| **Desempenho** | `PERFORMANCE` | Otimizações de velocidade e recursos |
| **Compatibilidade** | `COMPATIBILITY` | Ajustes para hardware, versões ou ambientes |
| **Segurança** | `SECURITY` | Melhorias de validação e proteção |
| **Removido** | `REMOVED` | Funcionalidades retiradas ou descontinuadas |
| **Alteração importante** | `BREAKING` | Mudanças que podem exigir atenção adicional |

### Funcionalidades da página

- pesquisa por texto;
- filtro por componente;
- filtro por categoria;
- ordenação cronológica;
- agrupamento por mês e ano;
- identificação de versões ainda não visualizadas;
- carregamento em segundo plano;
- utilização do último histórico válido quando estiver offline.

> [!IMPORTANT]
> A página apresenta alterações realizadas no projeto OMNIGHOST. Alterações oficiais dos jogos não são apresentadas como modificações desenvolvidas pelo launcher.

---

## ✦ Requisitos

### Sistema

| Requisito | Estado |
|:--|:--:|
| Windows 10 ou Windows 11 | Obrigatório |
| Sistema operativo x64 | Obrigatório |
| Permissão para executar a aplicação | Obrigatório |
| Espaço para aplicação e ficheiros temporários | Obrigatório |
| Ligação à Internet | Recomendada |

### Hardware

| Componente | Estado |
|:--|:--:|
| Placa DMA compatível | **Obrigatória para funcionalidades DMA** |
| Computador e ligações compatíveis com o setup | Obrigatório |
| Drivers adequados | Obrigatório |
| Firmware compatível | Obrigatório |
| MAKCU | Opcional |
| Fuser | Opcional |

### Resumo

```text
Placa DMA  ─────────────── Obrigatória
Windows x64 ────────────── Obrigatório
Drivers e firmware ─────── Obrigatórios
Internet ───────────────── Recomendada
MAKCU ──────────────────── Opcional
Fuser ──────────────────── Opcional
```

---

## ✦ Resolução de problemas

<details>
<summary><strong>◈ O launcher não abre</strong></summary>

<br>

- Confirma que extraíste todos os ficheiros.
- Não executes o programa dentro do ZIP.
- Move a aplicação para uma pasta simples, como `C:\OmniGhost\`.
- Confirma que o antivírus não removeu ficheiros necessários.
- Verifica se tens permissões para executar e escrever na pasta.

</details>

<details>
<summary><strong>◈ A placa DMA não é detetada</strong></summary>

<br>

- Confirma todas as ligações físicas.
- Reinicia os sistemas envolvidos.
- Verifica o firmware recomendado pelo fabricante.
- Confirma a instalação dos drivers necessários.
- Testa o equipamento com a ferramenta oficial do fabricante.
- Verifica se outro programa está a utilizar o dispositivo.

</details>

<details>
<summary><strong>◈ O MAKCU não aparece</strong></summary>

<br>

- Confirma que o MAKCU faz parte do teu setup.
- Verifica cabo, porta e alimentação.
- Confirma se o dispositivo é reconhecido pelo Windows.
- Reinicia o launcher depois de ligar o equipamento.

O MAKCU é opcional e a sua ausência não deve impedir a abertura normal do launcher.

</details>

<details>
<summary><strong>◈ O fuser não apresenta imagem</strong></summary>

<br>

- Confirma as entradas e saídas selecionadas.
- Verifica a resolução e frequência configuradas.
- Testa cada fonte de imagem separadamente.
- Confirma a alimentação e os cabos utilizados.

O fuser é opcional e depende da configuração física do utilizador.

</details>

<details>
<summary><strong>◈ Não foi possível verificar atualizações</strong></summary>

<br>

- Confirma a ligação à Internet.
- Tenta novamente após alguns segundos.
- Verifica se o GitHub abre normalmente no navegador.
- Confirma que uma firewall, VPN ou proxy não está a bloquear a aplicação.

Uma falha temporária na verificação não deve impedir a utilização normal do launcher.

</details>

<details>
<summary><strong>◈ Qual ficheiro devo descarregar?</strong></summary>

<br>

Descarrega sempre:

```text
OmniGhost.zip
```

Não utilizes os ficheiros automáticos de código-fonte como pacote do launcher.

</details>

---

## ✦ Suporte

Antes de reportar um problema, reúne as seguintes informações:

| Informação | Exemplo |
|:--|:--|
| Versão do OMNIGHOST | `1.6.4` |
| Versão do Windows | Windows 11 x64 |
| Modelo da placa DMA | Modelo utilizado |
| Firmware | Versão instalada |
| MAKCU | Utilizado / Não utilizado |
| Fuser | Utilizado / Não utilizado |
| Problema | Descrição clara |
| Reprodução | Passos para reproduzir |
| Evidências | Capturas e logs relevantes |

Inclui também:

- resultado esperado;
- resultado observado;
- frequência do problema;
- alterações recentes no setup;
- mensagem apresentada pela aplicação.

> [!WARNING]
> Remove tokens, credenciais, números de série, identificadores pessoais e outras informações sensíveis antes de publicar logs ou capturas.

---

## ✦ Estado do projeto

<div align="center">

[![Development](https://img.shields.io/badge/DEVELOPMENT-ACTIVE-D4AF37?style=for-the-badge&labelColor=101010)](#)
[![Platform](https://img.shields.io/badge/PLATFORM-WINDOWS%20x64-C9A227?style=for-the-badge&labelColor=101010)](#)
[![Hardware](https://img.shields.io/badge/HARDWARE-DMA-8D7627?style=for-the-badge&labelColor=101010)](#)

</div>

O OMNIGHOST encontra-se em desenvolvimento ativo.

A interface, integrações, requisitos e compatibilidade de hardware podem mudar entre versões. Consulta sempre as notas da versão mais recente antes de atualizar ou alterar o setup.

<div align="center">

[![Releases](https://img.shields.io/badge/CONSULTAR-RELEASES-D4AF37?style=for-the-badge&logo=github&logoColor=white&labelColor=101010)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases)
[![Latest](https://img.shields.io/badge/ABRIR-ÚLTIMA%20VERSÃO-C9A227?style=for-the-badge&logo=github&logoColor=white&labelColor=101010)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest)

</div>

---

<p align="center">
  <img
    width="100%"
    alt="OMNIGHOST footer"
    src="https://capsule-render.vercel.app/api?type=waving&height=130&section=footer&color=0:080808,50:D4AF37,100:080808"
  />
</p>

<div align="center">

## OMNIGHOST

**DMA · Biblioteca · Configuração · Atualizações**

<sub>Projeto independente para utilização responsável em ambientes autorizados.</sub>

</div>
