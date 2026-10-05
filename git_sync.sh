#!/bin/bash
#
# git_sync.sh - commit & push interaktif
#
# Dua mode:
#   1. Commit langsung ke branch yang sedang aktif
#   2. Buat branch baru dari posisi sekarang, lalu commit dan push
#
# Menu: Panah Atas/Bawah (atau k/j) = navigasi | ENTER = pilih | q / ESC = batal

REMOTE="origin"
MAX_LIST=12          # jumlah file yang ditampilkan di ringkasan perubahan

# ---------------------------------------------------------------------------
# Tampilan
# ---------------------------------------------------------------------------
C_RED=$'\e[31m'
C_GREEN=$'\e[32m'
C_YELLOW=$'\e[33m'
C_DIM=$'\e[2m'
C_RESET=$'\e[0m'

warn() { printf '%s%s%s\n' "$C_YELLOW" "$*" "$C_RESET"; }
die()  { printf '%s%s%s\n' "$C_RED" "$*" "$C_RESET" >&2; exit 1; }
step() { printf '%s-> %s%s\n' "$C_YELLOW" "$*" "$C_RESET"; }
run()  { local label="$1"; shift; step "$label"; "$@"; }

cleanup() { [ -t 1 ] && tput cnorm 2>/dev/null; }    # pastikan kursor selalu kembali
trap cleanup EXIT
trap 'printf "\n"; warn "Dibatalkan."; exit 130' INT

cancel() { clear; warn "Dibatalkan. Tidak ada yang diubah."; exit 0; }

# ---------------------------------------------------------------------------
# Pengecekan awal
# ---------------------------------------------------------------------------
[ "${BASH_VERSINFO[0]}" -ge 4 ] || die "Script ini butuh Bash 4 atau lebih baru."
[ -t 0 ] && [ -t 1 ] || die "Script ini interaktif: jalankan langsung dari terminal."
command -v git >/dev/null 2>&1 || die "Git belum terpasang."
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "Bukan di dalam repository Git."

# Selalu bekerja dari root repo, supaya 'git add' menangkap semua perubahan
cd "$(git rev-parse --show-toplevel)" || die "Gagal pindah ke root repository."

GITDIR=$(git rev-parse --git-dir)
if [ -d "$GITDIR/rebase-merge" ] || [ -d "$GITDIR/rebase-apply" ] || [ -f "$GITDIR/MERGE_HEAD" ]; then
    die "Ada merge/rebase yang belum selesai. Selesaikan atau batalkan dulu (git merge --abort / git rebase --abort)."
fi
[ -z "$(git ls-files -u)" ] || die "Ada file yang masih konflik. Selesaikan dulu sebelum sinkronisasi."

BRANCH=$(git symbolic-ref --quiet --short HEAD) \
    || die "HEAD sedang detached (tidak berada di branch mana pun). Pindah ke sebuah branch dulu."
git remote get-url "$REMOTE" >/dev/null 2>&1 || die "Remote '$REMOTE' tidak ditemukan."

git fetch --quiet "$REMOTE" 2>/dev/null \
    || warn "Tidak bisa menghubungi '$REMOTE' (offline?). Lanjut tanpa data terbaru."

# ---------------------------------------------------------------------------
# Daftar perubahan
# ---------------------------------------------------------------------------
PATHS=()
SHOWN=()
while IFS= read -r -d '' entry; do
    xy="${entry:0:2}"
    path="${entry:3}"
    if [[ $xy == R* || $xy == C* ]]; then      # rename/copy: entri berikutnya adalah path asal
        IFS= read -r -d '' _origin
    fi
    PATHS+=("$path")
    SHOWN+=("$xy $path")
done < <(git status --porcelain=v1 -uall -z)

if [ ${#PATHS[@]} -eq 0 ]; then
    echo "Tidak ada perubahan untuk di-commit."
    exit 0
fi

# File yang seharusnya TIDAK pernah masuk repo (jaring pengaman kalau .gitignore bocor)
is_protected() {
    local p="$1" base="${1##*/}"
    [[ $base == secrets.h ]] && return 0
    [[ $base == sdkconfig.* && $base != sdkconfig.defaults ]] && return 0
    [[ $p == .pio/* || $p == */.pio/* ]] && return 0
    [[ $p == components/esp_littlefs/* ]] && return 0
    return 1
}

FLAGGED=()
for p in "${PATHS[@]}"; do
    is_protected "$p" && FLAGGED+=("$p")
done

# ---------------------------------------------------------------------------
# Konteks yang tampil di atas setiap layar
# ---------------------------------------------------------------------------
UPSTREAM=$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null) || UPSTREAM=""

build_context() {
    printf 'Repo   : %s\n' "$(basename "$PWD")"
    printf 'Branch : %s%s%s' "$C_GREEN" "$BRANCH" "$C_RESET"
    if [ -n "$UPSTREAM" ]; then
        local ahead behind
        read -r ahead behind < <(git rev-list --left-right --count "HEAD...$UPSTREAM" 2>/dev/null)
        printf '  %s(%s: mendahului %s, tertinggal %s)%s' "$C_DIM" "$UPSTREAM" "${ahead:-?}" "${behind:-?}" "$C_RESET"
    else
        printf '  %s(belum punya upstream)%s' "$C_DIM" "$C_RESET"
    fi
    printf '\n'
    printf 'Perubahan (%d file):\n' "${#SHOWN[@]}"
    local i
    for i in "${!SHOWN[@]}"; do
        if [ "$i" -ge "$MAX_LIST" ]; then
            printf '  %s... dan %d lainnya%s\n' "$C_DIM" $(( ${#SHOWN[@]} - MAX_LIST )) "$C_RESET"
            break
        fi
        printf '  %s\n' "${SHOWN[$i]}"
    done
}

BASE_CONTEXT=$(build_context)
CONTEXT="$BASE_CONTEXT"

# ---------------------------------------------------------------------------
# Menu pilihan (panah atas/bawah, ENTER). Hasil: MENU_CHOICE (indeks mulai dari 0)
# select_menu "Judul" "Petunjuk" opsi1 opsi2 ...
# ---------------------------------------------------------------------------
select_menu() {
    local title="$1" hint="$2"
    shift 2
    local options=("$@")
    local n=${#options[@]} cursor=0 key key2 i

    tput civis 2>/dev/null
    while true; do
        clear
        printf '%s\n\n' "$CONTEXT"
        echo "$title"
        echo "$hint"
        echo "------------------------------------------------------------------"
        for i in "${!options[@]}"; do
            if [ "$i" -eq "$cursor" ]; then
                printf ' > %s%s%s\n' "$C_GREEN" "${options[$i]}" "$C_RESET"
            else
                printf '   %s\n' "${options[$i]}"
            fi
        done
        echo "------------------------------------------------------------------"

        IFS= read -rsn1 key || { tput cnorm 2>/dev/null; return 1; }
        case "$key" in
            $'\e')
                key2=""
                read -rsn2 -t 0.1 key2
                case "$key2" in
                    '[A') cursor=$(( (cursor - 1 + n) % n )) ;;
                    '[B') cursor=$(( (cursor + 1) % n )) ;;
                    '')   tput cnorm 2>/dev/null; return 1 ;;      # ESC saja = batal
                esac
                ;;
            k) cursor=$(( (cursor - 1 + n) % n )) ;;
            j) cursor=$(( (cursor + 1) % n )) ;;
            q) tput cnorm 2>/dev/null; return 1 ;;
            '') break ;;                                            # ENTER
        esac
    done
    tput cnorm 2>/dev/null
    MENU_CHOICE=$cursor
    return 0
}

# ---------------------------------------------------------------------------
# Input teks (tidak boleh kosong). Hasil: ANSWER
# ---------------------------------------------------------------------------
ask_nonempty() {
    local prompt="$1" value
    while true; do
        read -r -p "$prompt " value || return 1
        value="${value#"${value%%[![:space:]]*}"}"       # buang spasi di depan
        value="${value%"${value##*[![:space:]]}"}"       # buang spasi di belakang
        [ -n "$value" ] && break
        warn "Tidak boleh kosong."
    done
    ANSWER="$value"
}

valid_branch_name() {
    local name="$1"
    if [[ $name == -* ]]; then
        warn "Nama branch tidak boleh diawali tanda '-'."; return 1
    fi
    if ! git check-ref-format "refs/heads/$name" >/dev/null 2>&1; then
        warn "Nama branch tidak valid (tanpa spasi, '..', '~', '^', ':', '?', '*', '[', atau '\\')."; return 1
    fi
    if git show-ref --verify --quiet "refs/heads/$name"; then
        warn "Branch '$name' sudah ada di lokal."; return 1
    fi
    if git show-ref --verify --quiet "refs/remotes/$REMOTE/$name"; then
        warn "Branch '$name' sudah ada di $REMOTE."; return 1
    fi
    return 0
}

# ---------------------------------------------------------------------------
# Layar 1: peringatan file sensitif (kalau ada)
# ---------------------------------------------------------------------------
if [ ${#FLAGGED[@]} -gt 0 ]; then
    CONTEXT="$BASE_CONTEXT"$'\n\n'"${C_RED}PERINGATAN: file berikut biasanya tidak boleh masuk repo:${C_RESET}"
    for p in "${FLAGGED[@]}"; do
        CONTEXT+=$'\n'"  ${C_RED}${p}${C_RESET}"
    done
    select_menu "Lanjutkan tetap?" "(Panah Atas/Bawah: Navigasi | ENTER: Pilih | q: Batal)" \
        "Batal (perbaiki .gitignore dulu)" \
        "Tetap lanjutkan, aku tahu apa yang kulakukan" || cancel
    [ "$MENU_CHOICE" -eq 1 ] || cancel
    CONTEXT="$BASE_CONTEXT"
fi

# ---------------------------------------------------------------------------
# Layar 2: pilih mode
# ---------------------------------------------------------------------------
LABEL_DIRECT="Commit langsung ke '$BRANCH'"
case "$BRANCH" in
    main|master) LABEL_DIRECT="Commit langsung ke '$BRANCH'  (branch utama, hati-hati!)" ;;
esac

select_menu "Mau ngapain?" "(Panah Atas/Bawah: Navigasi | ENTER: Pilih | q: Batal)" \
    "$LABEL_DIRECT" \
    "Buat branch baru, lalu commit + push" \
    "Batal" || cancel

case "$MENU_CHOICE" in
    0) MODE="direct" ;;
    1) MODE="branch" ;;
    *) cancel ;;
esac

# ---------------------------------------------------------------------------
# Layar 3: input pesan commit (dan nama branch)
# ---------------------------------------------------------------------------
clear
printf '%s\n\n' "$CONTEXT"

ask_nonempty "Masukkan pesan commit:" || cancel
COMMIT_MSG="$ANSWER"

NEW_BRANCH=""
if [ "$MODE" = "branch" ]; then
    echo
    echo "Nama branch baru (boleh pakai prefix, mis. feat/nama-fitur atau fix/nama-bug)"
    while true; do
        ask_nonempty "Nama branch:" || cancel
        valid_branch_name "$ANSWER" && break
    done
    NEW_BRANCH="$ANSWER"
fi

# ---------------------------------------------------------------------------
# Layar 4: konfirmasi
# ---------------------------------------------------------------------------
SUMMARY="Yang akan dijalankan:"$'\n'
if [ "$MODE" = "branch" ]; then
    SUMMARY+="  1. git checkout -b $NEW_BRANCH   (dari '$BRANCH')"$'\n'
    SUMMARY+="  2. git add -A"$'\n'
    SUMMARY+="  3. git commit -m \"$COMMIT_MSG\""$'\n'
    SUMMARY+="  4. git push -u $REMOTE $NEW_BRANCH"
else
    SUMMARY+="  1. git add -A"$'\n'
    SUMMARY+="  2. git commit -m \"$COMMIT_MSG\""$'\n'
    if [ -n "$UPSTREAM" ]; then
        SUMMARY+="  3. git pull --rebase"$'\n'
        SUMMARY+="  4. git push"
    else
        SUMMARY+="  3. git push -u $REMOTE $BRANCH"
    fi
    case "$BRANCH" in
        main|master) SUMMARY+=$'\n\n'"${C_YELLOW}Perhatian: ini langsung ke branch utama '$BRANCH'.${C_RESET}" ;;
    esac
fi

CONTEXT="$BASE_CONTEXT"$'\n\n'"$SUMMARY"
select_menu "Jalankan?" "(Panah Atas/Bawah: Navigasi | ENTER: Pilih | q: Batal)" \
    "Ya, jalankan" \
    "Batal" || cancel
[ "$MENU_CHOICE" -eq 0 ] || cancel

# ---------------------------------------------------------------------------
# Eksekusi
# ---------------------------------------------------------------------------
clear
echo "Memproses..."
echo "--------------------------------"

if [ "$MODE" = "branch" ]; then
    run "Membuat branch '$NEW_BRANCH' dari '$BRANCH'..." git checkout -q -b "$NEW_BRANCH" \
        || die "Gagal membuat branch. Tidak ada yang diubah."
    TARGET="$NEW_BRANCH"
else
    TARGET="$BRANCH"
fi

run "Menambahkan perubahan..." git add -A || die "git add gagal."
if git diff --cached --quiet; then
    die "Tidak ada perubahan ter-stage (mungkin semuanya diabaikan oleh .gitignore)."
fi

run "Membuat commit..." git commit -q -m "$COMMIT_MSG" \
    || die "Commit gagal. Perubahanmu masih aman di working tree (branch: $TARGET)."
COMMIT_HASH=$(git rev-parse --short HEAD)

if [ "$MODE" = "branch" ] || [ -z "$UPSTREAM" ]; then
    run "Push ke $REMOTE/$TARGET..." git push -u "$REMOTE" "$TARGET" \
        || die "Push gagal. Commit $COMMIT_HASH aman di lokal. Coba lagi dengan: git push -u $REMOTE $TARGET"
else
    run "Menarik perubahan remote (rebase)..." git pull --rebase -q || {
        warn "Rebase gagal (kemungkinan konflik). Commit $COMMIT_HASH aman."
        warn "Selesaikan konfliknya di editor lalu jalankan: git rebase --continue"
        die "Atau batalkan dengan: git rebase --abort"
    }
    COMMIT_HASH=$(git rev-parse --short HEAD)      # hash berubah setelah rebase
    run "Push ke $REMOTE/$TARGET..." git push -q \
        || die "Push gagal. Commit $COMMIT_HASH aman di lokal. Coba lagi dengan: git push"
fi

echo "--------------------------------"
printf '%sSelesai!%s %s -> %s/%s\n' "$C_GREEN" "$C_RESET" "$COMMIT_HASH" "$REMOTE" "$TARGET"
if [ "$MODE" = "branch" ]; then
    echo "Kamu sekarang berada di branch '$TARGET'."
    echo "Buka Pull Request lewat tautan dari GitHub di atas, atau kembali ke branch lama: git checkout $BRANCH"
fi