# Kiwi Browser Local Build Scripts

Šie skriptai leidžia vykdyti visas operacijas lokaliai, o ne per GitHub Actions.

## Naudojami Skriptai

### 1. `build_apk_local.sh`
Sukuria APK failą lokaliai.

```bash
./local_scripts/build_apk_local.sh
```

**Reikalavimai:**
- Sukonfigūruota Chromium build aplinka
- `autoninja` komanda prieiname PATH

### 2. `rebase_kiwi_on_chromium_local.sh`
Atlieka kiwi branch rebase ant chromium branch.

```bash
./local_scripts/rebase_kiwi_on_chromium_local.sh
```

**Pastaba:** Po sėkmingo rebase reikia paleisti:
```bash
git push origin kiwi --force
```

### 3. `force_rebase_local.sh`
Atlieka force rebase su `--strategy-option ours` (pirmenybė teikiama kiwi pakeitimams).

```bash
./local_scripts/force_rebase_local.sh
```

### 4. `import_file_from_chromium_local.sh`
Importuoja vieną failą iš Chromium upstream.

```bash
./local_scripts/import_file_from_chromium_local.sh <kelias-iki-failo>
```

**Pavyzdys:**
```bash
./local_scripts/import_file_from_chromium_local.sh chrome/android/java/res/drawable-mdpi/ic_launcher.png
```

### 5. `update_chromium_files_local.sh`
Atnaujina visus Chromium failus į naują versiją.

```bash
./local_scripts/update_chromium_files_local.sh <major> <minor> <build> <patch>
```

**Pavyzdys:**
```bash
./local_scripts/update_chromium_files_local.sh 93 0 4577 25
```

### 6. `run_linter_local.sh`
Paleidžia kodo tikrinimą (linting) naudojant Docker.

```bash
./local_scripts/run_linter_local.sh
```

**Reikalavimai:**
- Įdiegtas Docker

## Teisių Nustatymas

Prieš naudojant skriptus, suteikite jiems vykdymo teises:

```bash
chmod +x local_scripts/*.sh
```

## Pastabos

- Visi skriptai automatiškai grįžta į pradinę branch po operacijos
- Skriptai naudoja spalvotas išvestis geresniam skaitomumui
- Klaidos atveju skriptai sustoja su aiškia klaidos žinute
- Git konfigūracija nustatoma automatiškai, jei nėra sukonfigūruota
