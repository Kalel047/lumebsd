//! Monta a imagem do pendrive do LumeBSD (ao vivo + instalador).
//!
//! O pendrive é um LumeBSD completo que dá boot sem tocar no disco do
//! computador. Ele leva:
//!   - /usr/share/lume/sistema.mtree: a lista dos arquivos do sistema (do
//!     METALOG), que o "lume instalar" usa para copiar o sistema limpo;
//!   - /usr/share/lume/pacotes: os pacotes essenciais, instalados no disco;
//!   - os mesmos drivers e firmwares já extraídos, para testar o hardware ao
//!     vivo (lume hardware, lume video testar, lume wifi).

use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};

use crate::sistema::{extras_lume, linhas_do_build, RAIZ};
use crate::util::{
    absoluto, apagar_se_existe, caminho, copiar, criar_pastas, dividir, gravar, ler, rodar, s,
    saida, sair, tamanho, topo,
};
use crate::v;

pub const USO: &str = "uso: lume-montar pendrive [--iso ISO] STAGE PACOTES FERRAMENTAS IMAGEM [VERSAO]

STAGE        saída de \"installworld distribution installkernel -DNO_ROOT\"
             (com o METALOG)
PACOTES      diretório com os pacotes essenciais (.pkg): pkg, driver de
             vídeo, firmwares e microcódigo
FERRAMENTAS  diretório com makefs, mkimg e kldxref do build
             (obj/.../tmp/legacy/bin)
IMAGEM       arquivo de saída
--iso ISO    também gera a mesma coisa em .iso híbrida (DVD ou pendrive,
             UEFI ou BIOS): a raiz é o próprio cd9660, só de leitura, e
             /etc, /var, /root, /mnt e /media vão para a memória no boot (o
             rc.initdiskless do FreeBSD)";

struct Pendrive {
    stage: PathBuf,
    ferramentas: PathBuf,
}

impl Pendrive {
    fn ferramenta(&self, nome: &str) -> String {
        s(&self.ferramentas.join(nome))
    }

    /// Escreve um arquivo no stage e devolve a linha do METALOG.
    fn escrever(&self, rel: &str, conteudo: &str, modo: &str) -> String {
        let dst = self.stage.join(rel);
        criar_pastas(dst.parent().unwrap());
        gravar(&dst, conteudo);
        format!("./{rel} type=file {RAIZ} mode={modo}")
    }
}

fn profundidade(l: &str) -> usize {
    let c = caminho(l);
    if c == "." {
        0
    } else {
        c.matches('/').count()
    }
}

pub fn main(args: &[String]) {
    let mut args: Vec<String> = args.to_vec();
    let mut iso = None;
    if args.first().map(String::as_str) == Some("--iso") && args.len() > 1 {
        iso = Some(absoluto(&args[1]));
        args.drain(..2);
    }
    if args.len() < 4 {
        sair(USO);
    }
    let stage = absoluto(&args[0]);
    let pacotes = absoluto(&args[1]);
    let ferramentas = absoluto(&args[2]);
    let imagem = absoluto(&args[3]);
    let versao = match args.get(4) {
        Some(v) => v.clone(),
        None => ler(topo().join("VERSAO")).trim().to_string(),
    };
    let serie = versao.rsplit_once('.').map(|(a, _)| a).unwrap_or(&versao).to_string(); // 1.2.0 -> 1.2
    let p = Pendrive { stage: stage.clone(), ferramentas };
    let metalog = stage.join("METALOG");
    // Sempre parte do METALOG do build, mesmo se rodar de novo.
    linhas_do_build(&stage);
    copiar(stage.join("METALOG.build"), &metalog);
    let texto = ler(&metalog);
    let mut it = texto.lines();
    let cabecalho = it.next().unwrap_or("").to_string();
    let linhas: Vec<String> = it.filter(|l| !l.trim().is_empty()).map(String::from).collect();

    // 1. Lista do sistema para o instalador e o "lume atualizar" (o METALOG
    //    sem tamanhos e hashes, incluindo a própria lista) e a versão
    //    completa: /usr/share/lume/sistema.mtree e /usr/share/lume/versao.
    let mut novas = extras_lume(&stage, &cabecalho, &linhas, &versao);
    // A mensagem de boas-vindas do sistema instalado (a do stage é trocada
    // pela do pendrive no passo 4; guarda a original na primeira vez).
    let motd = stage.join("etc/motd.template");
    let motd_build = stage.join("etc/motd.template.build");
    if !motd_build.exists() {
        copiar(&motd, &motd_build);
    }
    copiar(&motd_build, stage.join("usr/share/lume/motd.template"));
    novas.push(format!("./usr/share/lume/motd.template type=file {RAIZ} mode=0444"));

    // 2. Pacotes essenciais: copiados para o instalador e extraídos para uso
    //    ao vivo (menos o próprio pkg, que só faz falta no disco instalado).
    let destino = stage.join("usr/share/lume/pacotes");
    criar_pastas(&destino);
    let mut nomes: Vec<String> = fs::read_dir(&pacotes)
        .unwrap_or_else(|e| sair(format!("{}: {e}", pacotes.display())))
        .filter_map(|e| e.ok())
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n.ends_with(".pkg"))
        .collect();
    nomes.sort();
    for nome in &nomes {
        let arq = pacotes.join(nome);
        copiar(&arq, &destino);
        novas.push(format!("./usr/share/lume/pacotes/{nome} type=file {RAIZ} mode=0444"));
        if nome.starts_with("pkg-") {
            continue;
        }
        rodar(&v!["bsdtar", "-xf", s(&arq), "--exclude", "+*", "-C", s(&stage)], None);
        let listagem = saida(&v!["bsdtar", "-tvf", s(&arq)], None);
        for l in listagem.lines() {
            let partes = dividir(l, 9);
            if partes.len() < 9 || partes[8].trim_start_matches('/').starts_with('+') {
                continue;
            }
            let rel = partes[8].trim_start_matches('/');
            if partes[0].starts_with('d') {
                novas.push(format!("./{} type=dir {RAIZ} mode=0755", rel.trim_end_matches('/')));
            } else if partes[0].starts_with('-') {
                let modo = if partes[0].contains('x') { "0555" } else { "0444" };
                novas.push(format!("./{rel} type=file {RAIZ} mode={modo}"));
            } else if partes[0].starts_with('l') {
                if let Some((nome_link, alvo)) = rel.split_once(" -> ") {
                    novas.push(format!("./{nome_link} type=link {RAIZ} mode=0755 link={alvo}"));
                }
            }
        }
    }

    // 3. Índice dos módulos de terceiros (o kldload acha as dependências).
    rodar(&v![p.ferramenta("kldxref"), s(&stage.join("boot/modules"))], None);
    novas.push(format!("./boot/modules/linker.hints type=file {RAIZ} mode=0444"));

    // 4. Configuração do modo ao vivo.
    novas.push(p.escrever("etc/lume-ao-vivo", "LumeBSD rodando a partir do pendrive.\n", "0644"));
    novas.push(p.escrever("etc/fstab", "/dev/gpt/lumeroot\t/\tufs\trw,noatime\t1\t1\n", "0644"));
    novas.push(p.escrever(
        "etc/rc.conf",
        r#"hostname="lume-ao-vivo"
ifconfig_DEFAULT="DHCP"
dumpdev="NO"
kld_list="acpi_ibm coretemp"
devmatch_blocklist="if_iwx if_iwm"
lume_configurar_enable="NO"
lume_farol_enable="YES"
lume_notebook_enable="YES"
lume_video_enable="NO"
vigia_enable="YES"
"#,
        "0644",
    ));
    novas.push(p.escrever(
        "boot/loader.conf",
        &format!(
            r#"# LumeBSD {serie} - pendrive ao vivo
boot_multicons="YES"
# Só "efi": o comconsole falha num PC sem porta serial ("console comconsole
# failed to initialize", repetido na tela). Numa máquina com porta serial, o
# firmware já a põe no ConOut, e o kernel a usa de segunda (hw.uart.console).
console="efi"
# A tela é o console principal: sem isto, numa máquina virtual com porta
# serial o loader UEFI segue o firmware, que põe a serial na frente (veja
# também /boot/lua/local.lua, para o BIOS).
boot_serial="NO"
# Numa máquina virtual em BIOS, o console usaria o modo texto do VGA, cuja
# fonte não tem ã nem õ ("n?o"); o modo gráfico tem a fonte inteira.
hw.vga.textmode="0"
autoboot_delay="5"
kern.geom.label.disk_ident.enable="0"
kern.geom.label.gptid.enable="0"
cpu_microcode_load="YES"
cpu_microcode_name="/boot/firmware/intel-ucode.bin"
compat.linuxkpi.iwlwifi_11n_disable="0"
compat.linuxkpi.iwlwifi_disable_11ac="0"
"#
        ),
        "0644",
    ));
    // Em BIOS (SeaBIOS, o padrão do virt-manager e do GNOME Boxes), o loader
    // faz da serial o console principal sempre que "comconsole" está na
    // lista: as mensagens do boot e o rc iriam para ela, e a tela ficaria só
    // com o cursor. Lá, só "vidconsole"; a serial continua de segunda
    // (boot_multicons). O loader.lua roda este arquivo depois do loader.conf.
    novas.push(p.escrever(
        "boot/lua/local.lua",
        r#"-- LumeBSD: a tela é o console principal também em BIOS.
local core = require("core")
if not core.isUEFIBoot() then
	loader.setenv("console", "vidconsole")
end
"#,
        "0444",
    ));
    novas.push(p.escrever(
        "etc/motd.template",
        &format!(
            r#"
   LumeBSD {serie} "Farol"  -  pendrive ao vivo

   Nada no disco deste computador foi alterado.

     lume hardware        o que funciona aqui (comece por aqui)
     lume video testar    testa o driver de vídeo (Intel, AMD ou NVIDIA)
     lume wifi            conecta no Wi-Fi
     lume instalar        instala o LumeBSD no disco inteiro (APAGA TUDO)

   Você está como root, sem senha. Desligar: shutdown -p now

"#
        ),
        "0644",
    ));

    // 5. Junta tudo: as linhas novas substituem as antigas do mesmo caminho,
    //    e diretórios que faltam no METALOG são criados.
    let novos_caminhos: HashSet<String> = novas.iter().map(|l| caminho(l).to_string()).collect();
    let mut final_: Vec<String> = linhas
        .iter()
        .filter(|l| !novos_caminhos.contains(caminho(l)))
        .cloned()
        .chain(novas.iter().cloned())
        .collect();
    let mut vistos: HashSet<String> = final_.iter().map(|l| caminho(l).to_string()).collect();
    let mut dirs = vec![];
    for l in &final_ {
        let partes: Vec<&str> = caminho(l).split('/').collect();
        for i in 2..partes.len() {
            let d = partes[..i].join("/");
            if vistos.insert(d.clone()) {
                dirs.push(format!("{d} type=dir {RAIZ} mode=0755"));
            }
        }
    }
    final_.extend(dirs);
    // Pais antes dos filhos: ordena (de forma estável) pela profundidade.
    final_.sort_by_key(|l| profundidade(l));
    gravar(&metalog, format!("{cabecalho}\n{}\n", final_.join("\n")));

    // 6. Sistema de arquivos UFS e imagem GPT (UEFI e BIOS).
    let usado: u64 = saida(&v!["du", "-sk", s(&stage)], None)
        .split_whitespace()
        .next()
        .and_then(|x| x.parse().ok())
        .unwrap_or_else(|| sair("du: saída inesperada"));
    let tamanho_mb = usado / 1024 * 5 / 4 + 768;
    let pasta_saida = imagem.parent().unwrap().to_path_buf();
    let ufs = pasta_saida.join("pendrive-raiz.ufs");
    let fat = pasta_saida.join("pendrive-efi.fat");
    let efi = pasta_saida.join("pendrive-efi");
    for f in [&ufs, &fat, &imagem] {
        apagar_se_existe(f);
    }
    rodar(
        &v![
            p.ferramenta("makefs"), "-t", "ffs", "-B", "little",
            "-o", "version=2,label=lumeroot,softupdates=1",
            "-s", format!("{tamanho_mb}m"), "-F", "METALOG", "-N", "etc", s(&ufs), "."
        ],
        Some(&stage),
    );
    apagar_se_existe(&efi);
    criar_pastas(efi.join("EFI/BOOT"));
    copiar(stage.join("boot/loader.efi"), efi.join("EFI/BOOT/BOOTX64.EFI"));
    rodar(
        &v![
            p.ferramenta("makefs"), "-t", "msdos",
            "-o", "fat_type=32,sectors_per_cluster=1,volume_label=EFISYS",
            "-s", "64m", s(&fat), s(&efi)
        ],
        None,
    );
    rodar(
        &v![
            p.ferramenta("mkimg"), "-s", "gpt", "-f", "raw",
            "-b", s(&stage.join("boot/pmbr")),
            "-p", format!("efi/efiboot:={}", s(&fat)),
            "-p", format!("freebsd-boot/gptboot:={}", s(&stage.join("boot/gptboot"))),
            "-p", format!("freebsd-ufs/lumeroot:={}", s(&ufs)),
            "-o", s(&imagem)
        ],
        None,
    );
    fs::remove_file(&ufs).ok();
    fs::remove_file(&fat).ok();
    fs::remove_dir_all(&efi).ok();
    println!(
        "imagem: {} ({} MiB, sistema {} MiB)",
        imagem.display(),
        tamanho(&imagem) / (1 << 20),
        usado / 1024
    );

    if let Some(iso) = iso {
        gerar_iso(&p, &iso, &cabecalho, &final_, &pasta_saida);
    }
}

/// 7. A .iso híbrida: o mesmo sistema ao vivo, com a raiz no cd9660.
fn gerar_iso(p: &Pendrive, iso: &Path, cabecalho: &str, final_: &[String], pasta_saida: &Path) {
    let stage = &p.stage;
    let rotulo = "LUMEBSD";
    let fstab = stage.join("etc/fstab");
    let rcconf = stage.join("etc/rc.conf");
    let orig: Vec<(PathBuf, String)> = [&fstab, &rcconf].iter().map(|f| ((*f).clone(), ler(f))).collect();
    let mut extras = vec![
        p.escrever("etc/fstab", &format!("/dev/iso9660/{rotulo}\t/\tcd9660\tro\t0\t0\n"), "0644"),
        p.escrever(
            "etc/rc.conf",
            &format!(
                "{}# Na .iso a raiz é só de leitura; /etc, /var e /root ficam na\n\
                 # memória (/etc/diskless e /conf/base) e o /tmp também.\n\
                 root_rw_mount=\"NO\"\ntmpmfs=\"YES\"\ntmpsize=\"512m\"\n",
                orig[1].1
            ),
            "0644",
        ),
        p.escrever("etc/diskless", "", "0644"),
    ];
    // /conf/base/DIR: "remount" põe o DIR original (só de leitura) por baixo
    // do modelo; o rc.initdiskless cria um disco na memória do tamanho de
    // md_size (setores de 512 bytes) e copia tudo para ele.
    // /mnt e /media: onde o instalador e o usuário montam discos.
    for (d, setores) in [("etc", 131072), ("var", 1048576), ("root", 16384), ("mnt", 2048), ("media", 2048)] {
        extras.push(p.escrever(&format!("conf/base/{d}/remount"), &format!("mount_nullfs /{d}\n"), "0644"));
        extras.push(p.escrever(&format!("conf/base/{d}/md_size"), &format!("{setores}\n"), "0644"));
    }
    for d in ["conf", "conf/base", "conf/base/etc", "conf/base/var", "conf/base/root", "conf/base/mnt", "conf/base/media"] {
        extras.push(format!("./{d} type=dir {RAIZ} mode=0755"));
    }
    let trocados: HashSet<String> = extras.iter().map(|l| caminho(l).to_string()).collect();
    let mut linhas_iso: Vec<String> = final_
        .iter()
        .filter(|l| !trocados.contains(caminho(l)))
        .cloned()
        .chain(extras.iter().cloned())
        .collect();
    linhas_iso.sort_by_key(|l| profundidade(l));
    let meta_iso = stage.join("METALOG.iso");
    gravar(&meta_iso, format!("{cabecalho}\n{}\n", linhas_iso.join("\n")));

    // O ESP do boot UEFI vai dentro da .iso (El Torito).
    let esp_dir = pasta_saida.join("iso-efi");
    let esp = pasta_saida.join("iso-efi.fat");
    apagar_se_existe(&esp_dir);
    criar_pastas(esp_dir.join("EFI/BOOT"));
    copiar(stage.join("boot/loader.efi"), esp_dir.join("EFI/BOOT/BOOTX64.EFI"));
    apagar_se_existe(&esp);
    rodar(
        &v![
            p.ferramenta("makefs"), "-t", "msdos",
            "-o", "fat_type=12,sectors_per_cluster=1,volume_label=EFISYS",
            "-s", "2048k", s(&esp), s(&esp_dir)
        ],
        None,
    );
    // O mesmo ESP da .iso do FreeBSD: FAT12 de 2 MB, com o sistema de
    // arquivos do tamanho exato do arquivo (outros tamanhos o makefs faz
    // menores que o arquivo, ou vazios, e o UEFI não acha o boot).
    let setor = fs::read(&esp).unwrap_or_default();
    if setor.len() < 512
        || setor[510..512] != [0x55, 0xaa]
        || u16::from_le_bytes([setor[19], setor[20]]) as u64 * 512 != tamanho(&esp)
    {
        sair("montar-pendrive: o ESP da .iso saiu errado");
    }
    apagar_se_existe(iso);
    let publisher = "publisher=LumeBSD https://github.com/Kalel047/lumebsd";
    let cmd = v![
        p.ferramenta("makefs"), "-t", "cd9660",
        "-o", "rockridge", "-o", format!("label={rotulo}"),
        "-o", publisher,
        "-o", "bootimage=i386;boot/cdboot", "-o", "no-emul-boot",
        "-o", format!("bootimage=i386;{}", s(&esp)), "-o", "no-emul-boot",
        "-o", "platformid=efi",
        "-F", "METALOG.iso", "-N", "etc", s(iso), "."
    ];
    let ok = std::process::Command::new(&cmd[0])
        .args(&cmd[1..])
        .current_dir(stage)
        .status()
        .map(|st| st.success())
        .unwrap_or(false);
    // Volta o stage ao que era, tenha dado certo ou não.
    for (f, c) in &orig {
        gravar(f, c);
    }
    fs::remove_file(stage.join("etc/diskless")).ok();
    fs::remove_dir_all(stage.join("conf")).ok();
    fs::remove_file(&meta_iso).ok();
    if !ok {
        sair(format!("falhou: {}", cmd.join(" ")));
    }
    // Híbrida: GPT com o boot BIOS (isoboot) e o ESP na área de sistema da
    // .iso, para gravar num pendrive com dd (como a do FreeBSD).
    let et = saida(&v![p.ferramenta("etdump"), "-f", "shell", s(iso)], None);
    let mut espparam = vec![];
    for linha in et.split_whitespace() {
        let campos: HashMap<&str, &str> = linha.split(';').filter_map(|c| c.split_once('=')).collect();
        if campos.get("et_platform") == Some(&"efi") {
            let inicio: u64 = campos["et_lba"].parse::<u64>().unwrap_or(0) * 2048;
            let tam: u64 = campos["et_sectors"].parse::<u64>().unwrap_or(0) * 512;
            espparam = v!["-p", format!("efi::{tam}:{inicio}")];
            break;
        }
    }
    let hibrida = pasta_saida.join("iso-hibrida.img");
    let mut cmd = v![
        p.ferramenta("mkimg"), "-s", "gpt",
        "--capacity", tamanho(iso),
        "-b", s(&stage.join("boot/pmbr")),
        "-p", format!("freebsd-boot:={}", s(&stage.join("boot/isoboot")))
    ];
    cmd.extend(espparam);
    cmd.extend(v!["-o", s(&hibrida)]);
    rodar(&cmd, None);
    {
        use std::io::{Read, Seek, SeekFrom, Write};
        let mut h = fs::File::open(&hibrida).unwrap_or_else(|e| sair(format!("{e}")));
        let mut buf = vec![0u8; 32 * 1024];
        let n = h.read(&mut buf).unwrap_or(0);
        let mut f = fs::OpenOptions::new().write(true).open(iso).unwrap_or_else(|e| sair(format!("{e}")));
        f.seek(SeekFrom::Start(0)).ok();
        f.write_all(&buf[..n]).unwrap_or_else(|e| sair(format!("{}: {e}", iso.display())));
    }
    fs::remove_file(&hibrida).ok();
    fs::remove_file(&esp).ok();
    fs::remove_dir_all(&esp_dir).ok();
    println!("iso: {} ({} MiB)", iso.display(), tamanho(iso) / (1 << 20));
}
