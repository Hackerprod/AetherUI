# Avisos de terceros

Este repositorio referencia paquetes de terceros durante el build. Sus licencias continúan aplicando a esos componentes.

## Vortice.Windows / Vortice.Direct2D1

- Uso: bindings administrados para Direct2D, DirectWrite y tipos gráficos de Windows.
- Proyecto: https://github.com/amerkoleci/Vortice.Windows
- Paquete: https://www.nuget.org/packages/Vortice.Direct2D1
- Licencia declarada por el proyecto: MIT.

## Microsoft.CodeAnalysis.CSharp (Roslyn)

- Uso: API de source generators y compilación de las vistas `.cshtml` durante el build y en pruebas.
- Paquete: https://www.nuget.org/packages/Microsoft.CodeAnalysis.CSharp
- Licencia declarada por el paquete/proyecto: MIT.

AetherUI no copia código fuente de esos proyectos en este paquete. NuGet restaura las dependencias según `Directory.Packages.props`.
