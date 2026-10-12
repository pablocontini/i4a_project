# Portal: encabezados HTTP de Android

En Ramona, Android mostró `Header fields are too long` tanto en el portal
cautivo como al enviar usuario y contraseña desde Chrome a
`http://192.168.4.1`. Ese texto corresponde a HTTP 431 en ESP-IDF.
El límite anterior era `CONFIG_HTTPD_MAX_REQ_HDR_LEN=512`.

Se aumenta a 2048 bytes en la configuración activa y en `sdkconfig.defaults`
para conservarlo en nuevas compilaciones. La versión se identifica como
`redundant-link-20261008-ramona-hdr`.

Validación física pendiente: abrir el portal desde Android, iniciar sesión,
comprobar que carga la página administrativa y salir sin guardar cambios.
Un `ERR_CONNECTION_RESET` aislado no demuestra el mismo error. El reinicio
capturado por USB fue iniciado por el usuario para entrar a configuración.

Actualizar las cinco placas con la misma imagen: el control de arranque
compara sus versiones. No actualizar únicamente el central dejando el
resto con otra imagen.
