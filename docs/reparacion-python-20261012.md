# Reparación del entorno Python para ESP-IDF

El 11/10/2026 Windows empezó a rechazar tanto el Python embebido de Espressif
como su launcher venv por Control de aplicaciones. La política activa
VerifiedAndReputableDesktop coincidía con el evento CodeIntegrity 3077.
Los dos ejecutables anteriores estaban sin firma. No se demostró que las
extensiones de Python de VS Code provocaran ese cambio de autorización.

El 12/10 se instaló una distribución oficial separada de Python 3.11.9,
compatible con la familia utilizada en este proyecto. Se descargó de
python.org y se comprobó su firma válida de Python Software Foundation
antes de ejecutar el instalador. El proceso terminó con código cero.
La instalación anterior se conservó; no se cambió Smart App Control,
no se añadió una excepción, ni se cambió el PATH global.

La instalación nueva está en:

```text
%LOCALAPPDATA%\I4A-Repair\Python311
%LOCALAPPDATA%\I4A-Repair\idf541-env
```

El intérprete y el launcher del nuevo entorno virtual tienen firma válida.
Se instalaron las dependencias de requirements.core.txt de ESP-IDF 5.4.1
usando espidf.constraints.v5.4.txt. El comprobador oficial informa
`Python requirements are satisfied`; serial, esptool y click importan.
La variante de Grillo/Root ya compiló con ese entorno. Las 37 pruebas Python
también aprobaron usando el intérprete nuevo; el registro está en
`build/firmware/repaired-python-tests.log`.

Para seleccionarlo en la consola habitual de ESP-IDF (que ya contiene las
rutas de CMake, Ninja y compiladores), ejecutar estos comandos directamente:

```powershell
$env:IDF_PYTHON_ENV_PATH=Join-Path $env:LOCALAPPDATA 'I4A-Repair\idf541-env'
$env:PATH=(Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts')+';'+$env:PATH
python --version
python C:\esp\v5.4.1\esp-idf\tools\idf.py --version
```

Los cambios afectan únicamente esa sesión. No hace falta cambiar la política
de ejecución de scripts de PowerShell. La consola antigua de ESP-IDF puede
seguir seleccionando el launcher bloqueado; no usar su Python por defecto
sin verificar qué entorno quedó activo. Una nueva configuración CMake debe
seleccionar también este Python en su variable PYTHON para no conservar la
ruta vieja en el cache de un build anterior.

Si se configura VS Code, seleccionar el intérprete real del entorno nuevo:

```text
C:\Users\jp\AppData\Local\I4A-Repair\idf541-env\Scripts\python.exe
```

Las rutas son específicas de esta PC. No copiar el entorno virtual a otra PC
como sustituto de una instalación oficial. Python 3.11.9 fue elegido para esta
reparación concreta; no se afirma que sea la versión más reciente de Python.

Fuentes oficiales:
- https://www.python.org/downloads/release/python-3119/
- https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32/api-guides/tools/idf-windows-installer.html

El bloqueo no implicó modificar los nodos. La instalación de nuevos
firmwares se registra por separado en los logs y estados de OTA.
