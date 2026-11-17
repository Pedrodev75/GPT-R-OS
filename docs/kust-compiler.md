# Compilador Kust 0.1

`KUST.APP` é um binário GTRA v1.1 executado como aplicativo comum.
O terminal o encontra por meio do SYS64. O compilador recebe os
argumentos pela ABI GTRA e lê o fonte pelas syscalls de arquivo.

## Compilação

1. O lexer lê tokens sob demanda e registra linha e coluna.
2. A primeira passagem coleta constantes globais e assinaturas.
   O cursor inicial de cada corpo fica registrado na tabela de funções.
3. A segunda passagem interpreta os corpos com análise de escopo, tipos
   e retorno. O parser de precedência produz Assembly no mesmo percurso.
4. `kust build` envia o Assembly em memória à syscall 15. O assembler
   existente monta a imagem GTRA v1.1 com código, dados e BSS. O gravador
   FAT16 publica o resultado após a montagem e validação.
5. `kust check` termina após a análise e geração em memória. O assembler
   e os serviços de escrita só são usados em `build`.

O código gerado usa uma pilha de avaliação, argumentos de 8 bytes na
pilha em ordem de avaliação e resultado em EAX. Cada chamada nativa
traduz os argumentos para os registradores previstos na ABI. Literais
entram em `.data` como bytes decimais. Os registradores que guardam
endereços no suporte de memória ficam sob controle do código gerado.

## Limites e diagnóstico

| Recurso | Limite |
| --- | --- |
| Fonte | 32.768 bytes e 16.384 tokens |
| Assembly gerado | 65.536 bytes, com orçamento dentro dos 1.024 rótulos do assembler |
| Identificador e string no fonte | 31 e 512 bytes, respectivamente, sem aspas |
| Funções, parâmetros e constantes globais | 64, 8 por função e 64 |
| Variáveis locais alocadas | 64 por função |
| Profundidade combinada de expressões e blocos | 24 |
| Imagem executável | 65.536 bytes de código, dados e BSS |

Os buffers de fonte e saída ocupam 96 KiB do heap do compilador. A
workspace de análise fica na BSS. Diagnósticos incluem caminho, linha e
coluna quando aplicáveis. E001–E009 cobrem nomes, tipos, mutabilidade,
léxico, sintaxe, limites, declarações, semântica e geração. E010–E012
cobrem uso da CLI, leitura e montagem/publicação. Consulte
[ERRORS.TXT](kust/ERRORS.TXT) para a lista completa.

O compilador requer NX pela GTRA v1.1. A pilha do executável tem 16 KiB,
compartilhados pelos quadros de chamadas. Recursão fica limitada por
essa pilha. Strings são literais. Em falha de compilação, o APP
anterior permanece intacto. Falhas persistentes do disco podem deixar
incerto o estado da atualização, conforme a semântica de
[APP_ERR_UNCERTAIN na syscall de montagem](assembly-service.md).

## Bootstrap e imagem de distribuição

Use Python 3 e Zig 0.15.2 para a primeira compilação em host. O binário
resultante roda dentro do GPT-R OS sem depender dessas ferramentas.

```sh
python tools/build-kust.py
python tools/pack-image.py --boot boot.bin --loader loader.bin \
    --kernel kernel.bin --disk disco-base.img --output gpt-r-build.img
```

`build-kust.py` aceita `--zig` para selecionar o executável. Liga
o compilador no layout virtual da GTRA v1.1, valida as seções, aplica
o cabeçalho e informa tamanho em disco e espaço da imagem carregada.
O resultado padrão é `build/kust/KUST.APP`.

Use os binários de boot e kernel descritos em [Layout de boot](boot-layout.md).
O empacotador inclui por padrão o compilador, os capítulos TXT e os
exemplos KST em `/SYS64`. Ele usa a imagem-base como entrada e cria
outra imagem. Aceita a repetição de um bundle idêntico. Para um bundle
diferente, use uma base sem a instalação anterior. Uma colisão de nomes
com conteúdos diferentes interrompe o empacotamento. `--without-kust`
mantém o fluxo de empacotamento de desenvolvimento apenas do kernel.

## Testes

```sh
cc -std=c11 -Wall -Wextra -Werror -I. tests/kust_compiler.c \
    apps/kust/compiler.c kernel/apps/assembler.c kernel/apps/app_format.c \
    -o build/kust-tests
build/kust-tests docs/kust/EXAMPLES/*.KST
```

O teste em host compila cada exemplo com o mesmo núcleo e chama o
assembler do kernel para validar a saída. Para testar no sistema,
inicie uma cópia de `gpt-r-build.img` via QEMU.

```sh
qemu-system-x86_64 -m 128 -drive file=gpt-r-build.img,format=raw,if=ide -boot c
```

No Terminal, execute `kust help | cat`, `kust docs START | cat` e
`kust build /SYS64/DOCS/KUST/EXAMPLES/HELLO.KST | cat`. Depois verifique
`appinfo HELLO.APP` e execute `run HELLO.APP | cat`. Para o exemplo gráfico,
compile `GRAPHIC.KST` da pasta de exemplos, execute o APP e clique
dentro do quadro. Execute os exemplos em um diretório gravável.
