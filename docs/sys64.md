# SYS64 no FAT16

O GPT-R OS cria `/SYS64` na inicialização quando monta um FAT16 gravável e o nome está livre. Se a pasta já existir, a inicialização preserva seu conteúdo. Um arquivo com o mesmo nome ou uma falha de disco aparece no log. O boot prossegue e as demais pastas continuam disponíveis quando o volume permite.

## Ferramentas e resolução no Terminal

Use `/SYS64/NOME.APP`, com um nome de um a oito caracteres ASCII dentre letras, números, hífen e sublinhado. A pasta aceita executáveis GTRA v1 e v1.1. O caminho reservado para o compilador é `/SYS64/KUST.APP`.

O Terminal resolve na seguinte ordem.

1. Comandos internos têm precedência, inclusive quando digitados em maiúsculas. Argumentos inválidos continuam associados ao comando interno.
2. `run caminho.APP` ou um caminho explícito terminado em `.APP` abre aquele arquivo. O caminho pode ser absoluto ou relativo à pasta atual.
3. Um nome simples sem extensão, como `asmtest`, procura `NOME.APP` no SYS64 sem distinguir caixa. A busca usa apenas essa pasta. Para executar um aplicativo com nome igual ao de um comando interno, use um caminho explícito.

Um aplicativo encontrado por nome recebe o caminho resolvido em `argv[0]`. Os demais argumentos mantêm a interpretação de aspas, escapes e argumentos vazios de `run`. O diretório de trabalho continua sendo o do Terminal. Em um pipe, a saída e a entrada do aplicativo seguem as regras dos processos executados sem janela. [Formato GTRA e argumentos](gtra-v1.1.md)

## Instalação e atualização

Use `sysinstall ORIGEM.APP [NOME.APP]` para copiar um executável ao SYS64. Sem o nome de destino, o comando usa o nome curto da origem. Para substituir uma ferramenta existente, use `sysinstall -u ORIGEM.APP [NOME.APP]`. O comando valida o formato de origem e usa a escrita transacional do FAT16. A atualização recusa um destino aberto e pede que os aplicativos do SYS64 sejam fechados antes de substituir uma ferramenta.

A Central continua instalando GTRP em `/APPS`. A instalação em SYS64 é uma ação explícita do Terminal. O FAT16 protege a entrada da raiz e resolve a ascendência das subpastas para bloquear a criação, gravação, exclusão, movimentação e restauração no SYS64 por operações comuns. Ler, copiar para fora e executar permanecem disponíveis. Essa política evita acidentes. O comando de instalação é acessível no Terminal enquanto o GPT-R OS não tiver usuários e autenticação.

## Testar no QEMU

A imagem local inclui `/SYS64/ASMTEST.APP`, o demonstrador do serviço de montagem Assembly. Em uma cópia gravável, execute:

```text
mkdir PROJETOS
cd /PROJETOS
cp /ola.asm ola.asm
asmtest ola.asm TESTE.APP | cat
appinfo TESTE.APP
run TESTE.APP | cat
run /SYS64/ASMTEST.APP ola.asm LEGADO.APP v1 | cat
```

O arquivo de saída fica em `/PROJETOS`. Para conferir o bloqueio de escrita, use `echo teste > /SYS64/ASMTEST.APP`. O conteúdo do executável permanece igual. [Serviço de montagem](assembly-service.md)
