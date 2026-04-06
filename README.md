# HTTP Yuk Test Araci

C++17 ile yazilmis hizli, eszamanli HTTP kiyaslama araci. Bagimliliksiz. Platformlar arasi calisir.

**Zwenbabus** tarafindan gelistirilmistir.

---

## Derleme

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## Kullanim

```
./loadtest [secenekler] <url>

Secenekler:
  -n <sayi>    Toplam istek sayisi     (varsayilan: 100)
  -c <sayi>    Eszamanli is parcacigi  (varsayilan: 10)
  -m <str>     HTTP metodu             (varsayilan: GET)
  -b <str>     Istek govdesi
  -H <str>     Baslik (Anahtar:Deger)
```

## Ornekler

```bash
./loadtest -n 1000 -c 50 http://localhost:8080/api

./loadtest -n 200 -c 10 -m POST -b '{"anahtar":"deger"}' http://localhost:3000/veri

./loadtest -n 500 -c 25 -H "Authorization:Bearer token123" http://api.ornek.com/v1/kullanici
```

## Cikti

```
  Hedef      http://localhost:8080/api
  Sunucu     localhost (127.0.0.1:8080)
  Istekler   1000  Eszamanlilik 50

  100%  [1000/1000]

  Sonuclar  -----------------------------------------------
  Toplam istek  1000
  Basarili      998
  Basarisiz     2
  Istek/sn      843.21

  Gecikme  ------------------------------------------------
  ort          59.34 ms  [##########                    ]
  min           8.12 ms  [##                            ]
  maks        312.50 ms  [##############################]
  p50          52.10 ms  [#########                     ]
  p90         120.44 ms  [###########                   ]
  p99         289.33 ms  [############################  ]

  Durum Kodlari  -------------------------------------------
  200  998 istek
  503  2 istek
```

## Gereksinimler

- C++17 derleyici (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.14+
- Yalnizca HTTP desteklenir (TLS yok)

## Lisans

Apache 2.0
