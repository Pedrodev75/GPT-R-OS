# Revisão técnica do GPT-R OS v0.7.0

Base revisada `011e258`, com os Blocos 1 a 4. A revisão abrangeu boot, memória,
interrupções, execução GTRA, Terminal, FAT16 e ferramentas do desktop. Nenhum
problema crítico foi confirmado nos cenários revisados.

## Problemas corrigidos

| Gravidade e evidência | Arquivo e função | Causa e impacto | Correção aplicada |
|---|---|---|---|
| Alta · confirmado no QEMU | [Editor](../kernel/shell/shell_editor.c), `save_file` e `shell_edit_file` | O Editor mantinha uma cópia em memória e a gravava sobre a versão atual do arquivo. Uma alteração feita pelo Terminal com o Editor aberto era perdida. | Guardar os bytes originais antes de normalizar o texto e compará-los com o disco antes de salvar. Em conflito, o texto editado continua aberto e a versão do disco é preservada. |
| Alta · confirmado em teste automatizado e no QEMU | [FAT16](../kernel/fs/fat16_write.c), `fat16_file_changed`; [GTRA](../kernel/apps/app_files.c), `still_current`; [Terminal](../kernel/shell/shell.c), `run_pipeline`; [Explorador](../kernel/desktop/file_browser.c), `paste_file` | Duas gravações podem reaproveitar o cluster inicial e repetir tamanho e data. Comparar esses metadados permite aceitar uma versão diferente como se fosse a original. Isso podia sobrescrever um arquivo mais novo ou operar sobre a origem errada no Explorador. | Registrar mutações recentes por diretório e nome curto. A verificação detecta mudanças mesmo quando os metadados coincidem. A ABI GTRA usa o erro `APP_ERR_CHANGED` que já existia. Quando o histórico de 64 registros deixa de cobrir a operação, a verificação acusa conflito por precaução. |
| Alta · confirmada com injeção de falha | [FAT16](../kernel/fs/fat16_write.c), `fat_set` | Uma falha ao gravar o último espelho podia deixar as cópias divergentes. O gravador podia voltar a selecionar o mesmo cluster e falhar ao tentar usá-lo. | Reverter os espelhos modificados quando o setor ainda está legível e contém o valor tentado. A reversão inclui a cópia em que a escrita reportou erro, pois o setor pode ter mudado antes de uma falha de flush. A operação continua reportando o erro original. |
| Alta · causa confirmada por inspeção e correção validada no QEMU | [Lixeira](../kernel/desktop/trash.c), `trash_send` e `read_entry` | O envio aceitava um arquivo em pasta com nome OEM, mas a leitura do registro recusava os bytes desse caminho. O arquivo podia ficar fora da origem e invisível na Lixeira. | Validar o caminho e resolver o diretório original antes de mover. O carregamento do registro usa a mesma regra e preserva os bytes OEM que o resolvedor aceita. |

## Riscos e problemas pendentes

| Gravidade e estado | Arquivo e função | Causa e impacto | Correção recomendada |
|---|---|---|---|
| Alta · confirmado em volume sintético já corrompido | [FAT16](../kernel/fs/fat16_write.c), `valid_chain`, `release_chain` e `fat16_remove_at` | A validação segue a cadeia do arquivo e confere o comprimento e o fim. Ela não verifica se outro arquivo referencia os mesmos clusters. A exclusão em um volume com cadeias cruzadas tornou o segundo arquivo ilegível. | Validar a propriedade das cadeias antes de liberar clusters em volumes suspeitos e recusar gravações quando a propriedade for ambígua. A verificação exige uma visão do volume inteiro. |
| Alta · política de acesso confirmada por inspeção | [GTRA](../kernel/apps/app_files.c), `app_file_open` e `app_file_close` | Há validação de ponteiros, descritores e modo de abertura, mas as operações usam o FAT16 compartilhado. Um aplicativo com acesso de escrita pode alterar dados e metadados fora da sua pasta. | Tratar os `.APP` como código de confiança na versão atual. Centralizar permissões na futura VFS antes de executar aplicativos de terceiros sem essa premissa. |
| Média · confirmado com falha injetada | [FAT16](../kernel/fs/fat16_write.c), `fat16_mkdir` e `prepare_slot` | A pasta ou a extensão do diretório pode ter um cluster alocado antes de sua publicação. No teste de criação de pasta, uma falha no setor do diretório deixou um cluster ocupado sem uma entrada visível. | Recuperar alocações quando os metadados provarem que a entrada ficou sem publicação. Tratar cortes de energia e falhas durante a reversão com verificação do volume. |
| Média · comportamento confirmado por inspeção | [GTRA](../kernel/apps/app_files.c), `app_files_finish`; [kernel](../kernel/apps/app.c), `release_resources` | O fechamento automático grava cada arquivo separadamente em uma saída normal, inclusive com código diferente de zero. Se um fechamento posterior falhar, os anteriores podem ter sido gravados. | Preservar o contrato GTRA existente e exigir um mecanismo próprio de recuperação para qualquer operação futura que precise publicar vários arquivos como uma unidade. |
| Média · custo confirmado por inspeção, travamento prolongado em hardware não medido | [FAT16](../kernel/fs/fat16.c), `fat16_read_range`; [ATA](../kernel/drivers/ata.c), `wait_status`; [kernel](../kernel/apps/app.c), `service_file` | Leituras sucessivas com offset voltam a percorrer a cadeia, e o acesso PIO usa espera síncrona. Arquivos grandes ou um disco lento podem atrasar os eventos do desktop mesmo com os pipes cooperativos. | Manter um cursor por arquivo e dividir trabalho de disco em etapas com orçamento de tempo. Medir latência com arquivos grandes e I/O degradado. |
| Média · confirmado com imagem sintética | [Empacotador](../tools/pack-image.py), `pack` | Uma imagem FAT16 sem tabela de partições, fora do fluxo documentado, é aceita quando a área da tabela está zerada. O bootstrap acaba sobrescrevendo setores da FAT na imagem de saída. | Validar o layout de entrada e exigir uma partição de dados compatível antes de empacotar. |
| Média · confirmado por inspeção | [Lixeira](../kernel/desktop/trash.c), `scan`, `read_entry` e `restore`; [FAT16](../kernel/fs/fat16_write.c), `write_entry` | Cada registro exige uma busca do conteúdo no diretório. Com muitos itens, o custo cresce e a interface pode demorar a responder. A restauração via cópia preserva caminho e bytes e recria a entrada com a data padrão FAT16. | Dividir a listagem em passos e preservar os metadados que devam voltar com o arquivo. |
| Baixa · limitação confirmada por inspeção | [Gerenciador de Tarefas](../kernel/desktop/task_manager.c), `tasks_refresh`; [GTRA](../kernel/apps/app.c), `app_preempt` | CPU é uma amostra em modo usuário. Syscalls e I/O não entram nessa medida, e aplicações curtas podem terminar entre amostras. O tempo decorrido inclui espera. | Manter a descrição atual das métricas e medir tempo de kernel separadamente se houver necessidade de precisão maior. |
| Baixa · risco em hardware físico | [RTC](../kernel/drivers/timer.c), `rtc_read_time` e `from_bcd` | A leitura dupla reduz leituras partidas e o código verifica o intervalo final. Valores BCD inválidos ou um RTC sem hora válida podem passar pela conversão em casos de falha física. | Validar os dígitos BCD e o estado de validade do RTC. Exibir o estado indisponível quando a origem da hora falhar. |

## Verificação executada

- **Teste automatizado do FAT16 e da camada de arquivos GTRA.** Antes da correção, a falha transitória de escrita deixou os espelhos diferentes. O fechamento após a repetição de metadados retornou sucesso indevido. Depois da correção, os espelhos coincidiram tanto quando a falha ocorreu antes de gravar o setor como quando o setor já havia sido publicado. O arquivo original permaneceu íntegro, a próxima gravação funcionou e o fechamento concorrente retornou `APP_ERR_CHANGED` (`-7`). O teste cobre leitura concorrente, renomeação, gravação de um arquivo diferente no mesmo diretório e liberação dos buffers. [Código do teste](../tests/fat16_integrity.c)
- **Kernel e Bloco 1.** Compilação dos modos gráfico e texto com Zig 0.15.2 e `-Wall -Wextra -Werror`. No QEMU, o boot iniciou E820, IDT, PIT e FAT16. `memtest` liberou a página de teste e `crash heap` retornou `PASS`. `mkdir`, `cd`, `pwd`, `write`, `cp` e `mv` produziram os caminhos e bytes esperados em uma cópia do disco.
- **Bloco 2.** `asm ola.asm AUDIT.APP` gerou um GTRA executável. O carregador recusou um cabeçalho inválido, e `ARQTEST.APP` copiou **1.423 bytes idênticos**. A sequência prosseguiu após a recusa do arquivo inválido.
- **Bloco 3.** `run loop.APP | run nome.APP` devolveu o prompt nos dois modos. O teste do `>` fez duas gravações concorrentes pelo Editor, com reutilização do cluster e do tamanho. O Terminal exibiu **O destino mudou durante o comando** e preservou a versão mais nova. Após duas execuções com consumidor encerrando cedo e uma interrupção com Ctrl+C, o total retornou a **30.207 páginas livres de 4 KiB**. O redirecionamento cancelado ficou sem arquivo de saída.
- **Bloco 4.** No cenário de perda de dados, a versão pré-revisão gravou `xoriginal` sobre `externo`. A versão revisada preservou `externo` e manteve a edição aberta. O RTC passou de 13h07 para 13h08. Tema e formato do relógio persistiram após reiniciar. O Gerenciador de Tarefas mostrou memória de 8 KiB cair para 0 após encerrar o aplicativo. A Lixeira restaurou, após reiniciar, um arquivo de uma pasta com nome OEM e o conteúdo conferiu byte a byte. Os espelhos FAT coincidiram nas cópias verificadas.

Para repetir o teste de integridade com um compilador C11 que aceite os atributos GNU, execute na raiz do projeto.

```sh
mkdir -p build
cc -std=c11 -I. tests/fat16_integrity.c kernel/fs/fat16.c \
  kernel/fs/fat16_write.c kernel/fs/fat16_path.c kernel/apps/app_files.c \
  -o build/fat16-integrity
./build/fat16-integrity
```

## Limites e prioridades

Os testes usaram cópias descartáveis no QEMU e um dispositivo simulado para
injeção de falhas. Cortes reais de energia, falhas permanentes do controlador
ATA e RTC físico ficaram fora da validação. A micro revisão final ficou
restrita às mudanças acima e não encontrou outro problema alto que
justificasse alterar o código.

Antes de depender do FAT16 para operações com vários arquivos, a prioridade
é decidir como detectar cadeias cruzadas, como reagir a uma falha que deixe
metadados ambíguos e que garantias dar para recuperação. Os pontos de
permissões e portabilidade seguem como requisitos da futura VFS/ext2.
