# Login interactivo acotado (R3.1)

Demostración local sin autenticación, red ni envío de credenciales. El documento deriva de `native_login_static`, reducido exactamente a text, password y button. Use únicamente datos ficticios: la entrada estándar puede ser visible en el terminal y el frame puede mostrar el campo text. Password se presenta con bullets, nunca se imprime su valor. No es un formulario de producción ni un gestor de secretos.

Compile como C11 con el header público y enlace únicamente la biblioteca compartida AetherUI:

```sh
cc -std=c11 -Wall -Wextra -Werror -I native/include native/examples/native_login_interactive/main.c -L build -laether_core -Wl,-rpath,"$PWD/build" -o login-interactive
./login-interactive native/examples/native_login_interactive/login.html regular.ttf bold.ttf output.bgra
```

Adapte `build` al directorio real de la DSO. Las fuentes deben ser compatibles y tener licencia apropiada. El programa libera HTML y fuentes tras create, envía eventos y copia BGRA8 premultiplicado. No mide texto, dibuja glifos, decide foco ni hace hit-testing. La salida inicial mide 640×400, stride 2560. `resize` cambia dimensiones; el consumidor real consulta frame_info para presentar. El archivo se reemplaza tras cada render exitoso; no es PNG.

Comandos stdin (cada línea es un evento):

```text
keydown 1
text Demo
keydown 1
text Ficticio
keydown 1
keydown 2
keyup 2
keydown 3
keyup 3
resize 800 480
blur
quit
```

Keys: 1 Tab, 2 Enter, 3 Space, 4 Backspace. `keydown 1 1` significa Shift+Tab. Pointer: `down x y`, `up x y`, `cancel`. Las coordenadas están en píxeles del viewport; el motor decide el control. Se renderiza después de cada cambio exitoso para mantener vigente el frame necesario para pointer. `resize 0 400` suspende render, no el teclado; restaure dimensiones positivas para continuar. El host registra sólo evento, código, versión e identificador de activación, nunca payloads. No persiste activaciones ni las reproduce al renderizar.

La cuota de 256 MiB cubre sólo almacenamiento instrumentado, no memoria total/RSS: DOM/style/layout heredados están excluidos bajo G0-M1. Este ejemplo no acredita G1, QA de píxeles, retención ni 10.000 ciclos integrados.
