# Glosario

Términos que aparecen en la documentación, en orden alfabético.

| Término | Definición |
| --- | --- |
| **AEAD** | *Authenticated Encryption with Associated Data*. Cifrado que además garantiza que nada se ha modificado, incluidos unos «datos asociados» que van en claro. I-CPU usa XChaCha20-Poly1305. |
| **Argon2id** | Función de derivación de claves que convierte una contraseña en una clave y es lenta y costosa en memoria a propósito, para frenar los ataques de diccionario. Ganadora de la Password Hashing Competition (2015). |
| **BLAKE2b** | Función hash rápida y segura. En I-CPU deriva claves independientes a partir de la clave maestra. |
| **Bit menos significativo (LSB)** | El último bit de un número: cambiarlo varía el valor en ±1. Una muestra de 154 (`10011010`) pasa a 155 (`10011011`). |
| **Cabecera** | 30 bytes del formato v3 con el nonce y, cifrados, la versión, los parámetros y la longitud del mensaje. |
| **Capacidad** | Cuánto se puede ocultar en una imagen. Depende del formato de salida y del modo. |
| **Carga** (*payload*) | Lo que se oculta. Como medida, bits ocultos por ranura (por ejemplo, 0,25). |
| **Carga prudente** | Límite del modo adaptativo: 0,25 bits por ranura cambiable (0,20 en paleta). |
| **Chi-cuadrado** (χ²) | Test estadístico. Aplicado a pares de valores, detecta la sustitución LSB (Westfeld y Pfitzmann). |
| **Coeficiente DCT** | Número que guarda un JPEG en lugar de los píxeles: la intensidad de cada frecuencia en un bloque de 8 × 8. El DC es la media del bloque y los AC, los detalles. |
| **Coste** | En el modo adaptativo, lo que «se nota» cambiar una ranura. Bajo en zonas con textura, alto en zonas lisas. |
| **CSP** | *Content Security Policy*. Cabecera HTTP que limita qué puede cargar y ejecutar una web. |
| **DEFLATE** | Algoritmo de compresión sin pérdida (el de ZIP y PNG). |
| **DNS rebinding** | Ataque en el que una web maliciosa hace que su dominio apunte a 127.0.0.1 para hablar con servidores locales. I-CPU lo frena comprobando la cabecera `Host`. |
| **Dominio** | Dónde se esconden los bits: espacial (píxeles), DCT (coeficientes de un JPEG) o paleta (índices de un GIF). |
| **Esteganografía** | Ocultar la *existencia* de un mensaje. La criptografía oculta su *contenido*. |
| **Estegoanálisis** | Detectar si una imagen lleva un mensaje oculto. |
| **EzStego** | Técnica clásica para GIF: ordenar la paleta para que colores vecinos se parezcan y cambiar índices entre pares. |
| **Fuzzing** | Probar un programa con millones de entradas aleatorias o mutadas para encontrar fallos. |
| **HILL** | Función de coste para imágenes sin pérdida basada en un filtro de paso alto. |
| **Imagen portadora** (*cover*) | La imagen original, sin mensaje. |
| **Imagen estego** | La imagen que ya lleva el mensaje. |
| **k** | Bits por ranura en el modo de sustitución (1 a 4). |
| **LSB matching** | Modo que suma o resta 1 al azar cuando el bit no coincide, en lugar de sobrescribirlo. |
| **MAC** | *Message Authentication Code*. Etiqueta de 16 bytes (Poly1305) que prueba que el mensaje no se ha alterado. |
| **Modo adaptativo** | Modo por defecto: STC con costes, para cambiar pocas muestras y solo donde se notan menos. |
| **Muestra** | Cada valor de color de un píxel: un píxel RGB tiene tres muestras. |
| **NFC** | Forma de normalización Unicode: garantiza que «ñ» se codifique siempre igual. |
| **Nonce** | Número que se usa una sola vez. XChaCha20 usa uno aleatorio de 24 bytes. |
| **Orden secreto** | La permutación de las ranuras derivada de la contraseña: dónde va cada bit. |
| **Paleta** | Tabla de hasta 256 colores de un GIF; cada píxel guarda un índice. |
| **PSNR** | *Peak Signal-to-Noise Ratio*. Medida de distorsión en dB: más alto, más parecidas. |
| **Ranura** (*slot*) | Unidad donde cabe un bit: una muestra, un coeficiente DCT o un píxel de paleta. |
| **Ranura reservada** | Ranura que lleva la sal o la cabecera. |
| **Análisis RS** | Ataque de Fridrich que estima la fracción de muestras modificadas con sustitución LSB a partir de grupos *Regulares* y *Singulares*. |
| **Sal** | Valor aleatorio que se mezcla con la contraseña para que la misma contraseña dé claves distintas en cada imagen. En v3 son 16 bytes guardados en la imagen. |
| **Síndrome** | Resultado de multiplicar los bits por la matriz de paridad. En STC, el síndrome *es* el mensaje. |
| **SRM** | *Spatial Rich Model*. Detector moderno con miles de características estadísticas. |
| **SSIM** | *Structural Similarity*. Medida de similitud perceptual entre 0 y 1. |
| **STC** | *Syndrome-Trellis Codes*. Códigos que permiten ocultar un mensaje eligiendo los cambios de menor coste total. |
| **Sustitución LSB** | Modo que sobrescribe el bit menos significativo. El más sencillo y el más detectable. |
| **Token de sesión** | Valor aleatorio de 128 bits que la GUI exige en cada petición para que otras webs no puedan usarla. |
| **Trellis** | Grafo por capas donde se busca el camino de menor coste con el algoritmo de Viterbi. |
| **UERD** | Función de coste para JPEG basada en la energía de cada bloque y en la cuantización. |
| **W** | Ranuras de STC por cada bit de la sal y de la cabecera (1 a 64). |
| **XChaCha20-Poly1305** | Cifrado autenticado: XChaCha20 cifra y Poly1305 autentica. |
