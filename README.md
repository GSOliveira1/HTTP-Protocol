# Trabalho Prático - Redes de Computadores
Cliente e servidor web desenvolvido em C para a disciplina de Redes de Computadores do curso de Ciências da Computação da UFSJ com o objetivo de aprofundar o aprendizado do protocolo HTTP.

## Propósito
Implementar, em C, um par **Cliente/Servidor HTTP** mínimo para:
- **Cliente**: envia 'GET' para 'htpp://host[:porta]/caminho' e tratar o corpo recebido (salvar arquivo ou exibir listagem quando for diretório).
- **Servidor**: mapear caminhos para um diretório local, servir 'index.html' (ou outros arquivos) quando existir ou listar arquivos quando nçao houver, e retornar códigos simples (200, 403, 404, 405).

## Compilar
```bash
make
```

## Executar (Exemplos)

- ./meu_servidor ./dir
- ./meu_navegador http://localhost:5050/index.html
- ./meu_navegador http://localhost:5050/image.jpg