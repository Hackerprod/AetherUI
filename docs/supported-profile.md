# Perfil soportado: AetherUI Desktop Profile 0.1

Este documento es contractual para la alpha. “HTML/CSS” significa el subconjunto enumerado; no implica conformidad con toda la plataforma web.

## HTML y DOM

### Elementos renderizados o reconocidos

- estructura: `html`, `head`, `body`, `main`, `section`, `article`, `header`, `footer`, `nav`;
- bloques: `div`, `form`, `p`, `h1`–`h6`;
- inline: `span`, `label`, `strong`, `em`, `a`;
- controles: `input`, `button`;
- especiales: `br`, `style`, `script`, `title`, `meta`, `link`.

`script` se analiza como raw text pero nunca se ejecuta y su estilo de agente de usuario es `display:none`.

### DOM

- árbol padre/hijos;
- `TextContent`;
- atributos;
- `id` y clases;
- `GetElementById`;
- `GetElementsByTagName`;
- `QuerySelector` básico para tag, `.clase` y `#id`;
- mutación mediante append/insert/remove/clear;
- índice de IDs invalidado por versión del documento.

### Parser

- tags de inicio/cierre;
- atributos quoted/unquoted y booleanos;
- entidades HTML mediante `WebUtility.HtmlDecode`;
- comentarios y declaraciones;
- void elements comunes;
- raw text para `style` y `script`;
- recuperación simple ante cierres desbalanceados.

No implementa el algoritmo completo WHATWG, foster parenting, adoption agency, namespaces HTML/SVG/MathML ni reglas completas de autocierre.

## Selectores CSS

- selector universal `*`;
- tag;
- `#id`;
- `.clase`;
- combinación de tag/id/clases;
- atributo `[name]` y `[name=value]`;
- descendiente `A B`;
- hijo directo `A > B`;
- grupos separados por coma;
- `:hover`, `:active`, `:focus`, `:disabled`, `:required`, `:invalid`, `:first-child`, `:last-child`.

No hay `:not`, `:is`, `:has`, nth selectors, sibling combinators ni pseudoelementos.

## Cascada

- stylesheet de agente de usuario interno;
- reglas `<style>` del documento;
- atributo inline `style`;
- especificidad básica;
- orden de fuente;
- `!important`;
- herencia de color, fuente, tamaño, peso, line-height, text-align y cursor.

La expansión de shorthands y todos los detalles de origen/capas CSS todavía no son conformes al estándar completo.

## Propiedades CSS

### Layout y caja

- `display`: `none`, `block`, `inline`, `inline-block`, `flex`;
- `width`, `height`, `min-width`, `min-height`, `max-width`, `max-height`;
- `margin` y lados;
- `padding` y lados;
- `border`, `border-width`, lados de ancho, `border-color`, `border-radius`;
- `box-sizing`;
- `overflow: visible|hidden`;
- `visibility`.

### Flexbox básico

- `flex-direction: row|column`;
- `justify-content: start|center|end|space-between`;
- `align-items: stretch|start|center|end`;
- `gap`.

`flex-grow` se reconoce en estilo computado pero la distribución flexible completa no está implementada. No hay wrap, shrink, basis, order ni align-self.

### Apariencia y texto

- `background`/`background-color` para colores sólidos;
- `color`;
- `opacity` aplicada durante paint;
- `font-family` (primera familia);
- `font-size`;
- `font-weight`;
- `line-height`;
- `text-align`;
- `cursor: default|pointer|text`.

### Unidades

- `px`, `%`, `em`, `rem`, `vw`, `vh`, `auto`;
- cero y, como extensión del perfil, números unitless tratados como DIPs en propiedades de longitud.

### Colores

- hex `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`;
- `rgb(...)`, `rgba(...)` con comas;
- conjunto pequeño de nombres (`black`, `white`, `red`, `blue`, etc.);
- `transparent`.

No hay gradientes, imágenes de fondo, sombras, filtros, transforms, transitions ni animations.

## Layout

- flujo block vertical;
- fragmentos inline y wrapping por tokens de texto;
- `br`;
- controles `input`/`button` atómicos;
- Flexbox unidimensional básico;
- box model y dimensiones mínimas/máximas;
- clipping por `overflow:hidden`;
- hit testing en orden inverso de paint.

No hay margin collapsing, floats, position, stacking contexts, z-index, tables, Grid, multicolumn, scroll containers ni writing modes.

## Texto

- medición y drawing DirectWrite en Windows;
- Unicode soportado por DirectWrite y las fuentes instaladas;
- font fallback dependiente del sistema;
- wrapping inline básico;
- caret al final de `<input>`.

No hay selección, navegación de caret, shaping bidireccional controlado por el motor, password masking, multiline text, IME/TSF ni clipboard.

## Controles y formularios

### `<input>`

- valor controlado mediante `value`;
- valor no controlado conservado por `id` entre renders;
- `placeholder`;
- `required`;
- `maxlength`;
- entrada de texto al final;
- backspace/delete al final;
- Enter para enviar el form más cercano.

### `<button>`

- click por mouse;
- activación por Enter/Space cuando tiene foco;
- `disabled`;
- `type="button"` evita submit;
- otros tipos se tratan como submit en esta alpha.

### Validación

- `required` no vacío;
- estado `:invalid`;
- evento `invalid`;
- submit bloqueado cuando falla validación.

## Eventos

- pointer/mouse: `mouseover`, `mouseout`, `mousedown`, `mouseup`, `click`;
- foco: `focus`, `blur`;
- teclado: `keydown`;
- edición: `input`;
- formulario: `submit`, `invalid`;
- bubbling por ancestros;
- `PreventDefault()`;
- `StopPropagation()`.

No hay capture phase, listener options, pointer IDs, wheel, drag/drop, touch, composition events ni eventos asíncronos.
