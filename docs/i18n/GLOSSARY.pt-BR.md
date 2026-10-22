# Glossário pt-BR (Haumea Studio)

Translators of `tools/i18n/pt-BR/*.po` and `tools/haumea/lang/pt-BR.po` follow this
list. English is the key; keep the tone short and direct, as the UI is. Brazilian
spelling, imperative or infinitive on buttons: **Salvar**, **Abrir**, **Excluir**.

## Kept in English

These are the terms SA-MP and open.mp users know by name; they also keep Pawn, native
and streamer names, model and texture names, file names and config keys.

| English | pt-BR |
|---|---|
| gang zone | gang zone |
| pickup | pickup |
| checkpoint | checkpoint |
| race checkpoint | race checkpoint |
| NPC | NPC |
| textdraw | textdraw |
| 3D text label | 3D text label |
| map icon | map icon |
| workspace | workspace |
| prefab | prefab |
| streamer | streamer |
| Pawn, SA-MP, open.mp, GTA | (unchanged) |
| LOD | LOD |
| IPL, IDE, DFF, TXD, COL | (unchanged) |

## Translated

| English | pt-BR |
|---|---|
| Object | Objeto |
| Object Info | Informações do objeto |
| Object Browser | Navegador de objetos |
| Map Objects | Objetos do mapa |
| Tools | Ferramentas |
| Preferences | Preferências |
| Settings | Configurações |
| Language | Idioma |
| View | Exibir |
| Log | Registro |
| (none) | (nenhum) |
| No workspace | Sem workspace |
| Save / Open / Close | Salvar / Abrir / Fechar |
| Delete | Excluir |
| Cancel / Apply / Reset | Cancelar / Aplicar / Redefinir |
| Undo / Redo | Desfazer / Refazer |
| Copy / Paste / Duplicate | Copiar / Colar / Duplicar |
| Select / Selected | Selecionar / Selecionado(s) |
| Search / Filter | Buscar / Filtro |
| Name | Nome |
| Position / Rotation / Size | Posição / Rotação / Tamanho |
| Camera | Câmera |
| Texture / Material / Model | Textura / Material / Modelo |
| Vehicle / Actor | Veículo / Ator |
| Area | Área |
| Map | Mapa |
| Window / Panel | Janela / Painel |
| Server | Servidor |

## Rules

- Keep every `%d`, `%s`, `%.1f`, `%%` and `\n` of the English text, in the same order.
  `python3 tools/i18n/extract.py --catalogs` finds the ones that differ; the editor
  ignores a translation with different conversions.
- `Text##id` is translated as `Text`; the `##id` part is not in the catalog.
- Keep keyboard keys (Ctrl, Shift, Alt, F6, RMB) as they are written.
- Do not translate language names; each catalog names itself in its header.
- A string that stays as it is (a term above, a unit like "m", "px") still gets an
  entry with `msgstr` equal to `msgid`, so the completeness test passes.
- The label is wider in Portuguese by ~20%: prefer the shorter word on buttons.
