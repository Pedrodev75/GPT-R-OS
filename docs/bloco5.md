# Bloco 5 da v0.7.0

A Central de Aplicativos fica no menu do desktop. Para instalar, abra um `.PKG` pelo Explorador ou execute `pkg VELHA.PKG` no Terminal. Confira os dados e pressione **Enter** ou clique em **Instalar**. Na lista de instalados, use **↑/↓** para selecionar, **←/→** para trocar de página, **Enter** para executar e **Delete** seguido de **Enter** para confirmar a remoção.

O pacote oficial `VELHA.PKG` instala `/APPS/VELHA/VELHA.APP`. O jogo usa as chamadas gráficas e os eventos de mouse do GTRA v1. X começa, cada jogada válida troca o turno, e o resultado encerra a partida até que alguém clique em **Reiniciar**. **Sair** encerra o processo e devolve suas páginas ao kernel.

No Editor, **Enter** copia a indentação inicial da linha corrente, **Tab** avança até a próxima coluna múltipla de quatro e **Shift+Tab** remove um nível inicial da linha. **Ctrl+Z** e **Ctrl+Y** percorrem até 16 edições. Uma edição após desfazer descarta o ramo de refazer. Em fontes `.ASM`, rótulos, instruções e comentários têm cores distintas. Um salvamento sem edições preserva os bytes originais. Após edição, o Editor mantém a política anterior de gravar quebras em LF. O limite continua em 4 KiB. O Editor compara o arquivo no disco com os bytes originais antes de gravar.

## GTRP v1

Todos os inteiros são little-endian. O cabeçalho tem 16 bytes.

| Deslocamento | Campo | Tipo |
| --- | --- | --- |
| 0 | Assinatura `GTRP` | 4 bytes |
| 4 | Versão 1 | u16 |
| 6 | Tamanho do cabeçalho, 16 | u16 |
| 8 | Tamanho do manifesto | u32 |
| 12 | Número de arquivos | u16 |
| 14 | Índice do executável principal, começando em zero | u16 |

Segue um manifesto UTF-8 com uma linha `chave=valor` por campo. `id`, `name` e `version` são obrigatórios. `description` é opcional. O ID tem de um a oito caracteres ASCII dentre letras, dígitos, hífen e sublinhado, e é convertido para maiúsculas. Os demais valores têm até 80, 24 e 256 bytes, respectivamente. A tabela começa depois do manifesto e tem uma entrada de 20 bytes por arquivo.

| Deslocamento na entrada | Campo | Tipo |
| --- | --- | --- |
| 0 | Nome curto 8.3, completado com zeros se ocupar menos de 12 bytes | 12 bytes |
| 12 | Tamanho em bytes | u32 |
| 16 | CRC-32 IEEE | u32 |

Os dados seguem a tabela na mesma ordem e sem compressão ou preenchimento. O pacote tem no máximo 16 arquivos e 256 KiB, incluindo os metadados. O manifesto ocupa até 1024 bytes. Os nomes de arquivos permitem os mesmos caracteres do ID, um ponto separando a extensão opcional e uma única caixa por componente. Caminhos e aliases duplicados são rejeitados. `PKG.DAT` é reservado. A entrada principal precisa ser um `.APP` GTRA v1 válido. O CRC detecta corrupção acidental. O formato não autentica o publicador.

## Registro e recuperação

O gerenciador de pacotes opera sobre uma interface de armazenamento. O adaptador FAT16 resolve nomes, controla criação exclusiva e faz I/O. Um adaptador VFS/ext2 pode preservar o mesmo formato de distribuição.

A Central mantém `/APPS/<ID>/PKG.DAT`. O registro guarda a assinatura interna `GTPI`, versão e estado, seguidos dos metadados do pacote. A instalação valida o conteúdo antes de gravar, persiste o estado pendente, cria cada arquivo exclusivamente e relê tamanho e CRC. O estado instalado é publicado depois dessas verificações. A Central lista operações interrompidas com a ação **Limpar** quando encontra um registro pendente legível.

A remoção verifica os nomes, tamanhos e CRC antes de apagar cada arquivo conhecido e bloqueia a operação enquanto houver um processo do diretório em execução. Arquivos alterados ou desconhecidos continuam no disco. A pasta e o registro inativo ficam disponíveis para futuras instalações, que verificam novamente cada conflito. Se houver interrupção entre a criação da pasta e a publicação do primeiro registro, a pasta aparece como conflito e é preservada para inspeção. O FAT16 continua sujeito aos limites de recuperação do driver e à integridade prévia do volume.

A Central faz a instalação e a remoção no mesmo fluxo de eventos do desktop. Pacotes próximos ao limite podem levar alguns segundos enquanto o driver ATA grava e confirma os setores.

## Gerar o pacote oficial

Em ambiente x86-64 com ferramentas GNU, monte `apps/VELHA.S` e extraia a seção de código. Passe o binário bruto ao empacotador GTRA existente, `tools/Pack-App.ps1`, usando `VELHA.APP` como nome de saída.

```sh
as --64 apps/VELHA.S -o velha.o
objcopy -O binary -j .text velha.o velha.bin
```

```powershell
.\tools\Pack-App.ps1 -InputPath velha.bin -OutputPath VELHA.APP
python tools/pack-pkg.py --id VELHA --name "Jogo da Velha" --version 1.0.0 --description "Dois jogadores locais. Complete uma linha, coluna ou diagonal." --main VELHA.APP --output VELHA.PKG VELHA.APP
```

## Teste automatizado

Crie um pacote de teste com `VELHA.APP` como primeira entrada e um arquivo não vazio chamado `data.txt` como segunda. O teste abaixo usa o parser, o validador GTRA e o instalador do kernel com armazenamento em memória. Ele injeta falhas de gravação e de limpeza e verifica a recuperação pelo registro que restou.

```sh
cc -std=c11 -Wall -Wextra -Werror tests/package_integrity.c kernel/packages/format.c kernel/packages/install.c kernel/apps/app_format.c -o package_test
./package_test test.PKG
```

## Validação da build

As variantes gráfica e texto foram compiladas. A validação de interface foi feita na variante gráfica, usando cópias FAT16 descartáveis no QEMU.

| Área | Verificações executadas |
| --- | --- |
| Pacotes | Abertura pelo Terminal e Explorador, instalação, lançamento após reinício, duplicata, remoção, reinstalação, recuperação de remoção interrompida, preservação de arquivos e leitura de volta de um pacote com arquivo de 250.000 bytes. A Central rejeitou CRC incorreto, truncamento e cabeçalho GTRA inválido. O teste em memória validou nomes e aliases inválidos, disco cheio, conflito de nomes e falhas de gravação e de limpeza. |
| Jogo | Vitórias de X e O, empate, casa ocupada, reinício e saída. Após encerrar, o Gerenciador de Tarefas mostrou zero processos ativos e 0 KiB no jogo. |
| Editor | Indentação automática, Tab e Shift+Tab, 16 comandos de desfazer seguidos de 16 para refazer, cores ASM, salvamento, preservação de CRLF sem edição, conflito de gravação e limite de 4 KiB. |
| Blocos 1–4 | `memtest`, `crash heap`, montagem e execução de GTRA, pipes e redirecionamento, persistência de tema e relógio, envio à Lixeira e restauração. O teste automatizado FAT16/GTRA existente também passou. |
