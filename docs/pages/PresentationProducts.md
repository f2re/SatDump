# Presentation-ready image products

SatDump 1.2.2 Presentation adds a non-destructive final rendering stage to the existing product-processing pipeline.

The scientific image remains unchanged. The presentation layer writes additional files:

```text
<product>.png / <product>.tif              existing SatDump output
<product>_annotated.png                    presentation-ready output
<product>_annotated.json                   machine-readable presentation passport
```

The annotated image is not treated as a geospatial raster. Headers and legends occupy pixels outside the original Earth-observation raster, so the original projected TIFF/PNG remains the source for GIS and quantitative processing.

## Rendering priority

For each generated composite, the renderer selects the most complete available variant in this order:

1. geographic projection with map overlays;
2. geometrically corrected image with map overlays;
3. image with map overlays;
4. geometrically corrected image;
5. base composite.

Only one annotated image is generated for a composite. Existing SatDump outputs are still generated exactly as before.

## Visual system

The standard theme follows an 8-pixel-derived spacing system and a restrained scientific palette.

| Token | Default |
|---|---|
| Header/footer background | `#0E1624` |
| Secondary surface | `#172235` |
| Primary text | `#F3F7FB` |
| Secondary text | `#AAB8C8` |
| Accent | `#4EC7E8` |
| Warning | `#FFB454` |
| Error | `#FF6B6B` |

Typography and spacing scale with raster width and are clamped to readable minimum and maximum values. The source image is copied without resampling.

The bundled `Roboto-Medium.ttf` font is used by SatDump 1.2.2 and supports UTF-8 labels, including Russian metadata and legend text.

## Header content

The header is assembled from data that are actually present in the product. Missing values are omitted rather than guessed.

Supported fields include:

- spacecraft / product source;
- instrument;
- product or composite name;
- acquisition start and end in UTC;
- pass direction and maximum elevation, when recorded;
- NORAD ID;
- channels and central wavelengths;
- receiver centre frequency and sample rate, when recorded;
- projection type;
- selected output variant;
- quality score, packet loss and SNR, when recorded.

Reception frequency and spectral wavelength are deliberately shown as separate concepts.

## Legend modes

### Composite legend

Composite legends are generated automatically when no explicit legend is configured.

For three- and four-output equations the renderer identifies R, G, B and A components, then records:

- the expression used for the component;
- every `ch...` and `cch...` input token;
- the matching SatDump channel name;
- central wavelength when calibration metadata are present;
- whether the input is raw or calibrated;
- equalization, inversion, normalization, white balance and LUT processing.

For LUT, Lua and C++ composites, the renderer lists all discoverable input channels and adds an explicit note that a resulting colour can depend on several input values.

A composite footer therefore never remains empty merely because a one-dimensional physical colour scale would be misleading.

### Continuous legend

Use a continuous legend for a quantitative scalar field such as brightness temperature, reflectance, cloud-top height or precipitation rate.

```json
{
  "presentation": {
    "enabled": true,
    "title": "Яркостная температура облачной поверхности",
    "legend": {
      "kind": "continuous",
      "title": "Яркостная температура",
      "subtitle": "Канал 10,8 мкм; значения приведены в физических единицах",
      "unit": "K",
      "min": 180.0,
      "max": 320.0,
      "tick_count": 8,
      "colors": [
        "#1B1844",
        "#1E5E8B",
        "#24A093",
        "#96C959",
        "#FCE725"
      ],
      "notes": [
        "Температура является яркостной температурой излучающей поверхности, если отдельный алгоритм восстановления температуры верхней границы облаков не применён."
      ]
    }
  }
}
```

Ticks can be supplied explicitly as values:

```json
"ticks": [180, 200, 220, 240, 260, 280, 300, 320]
```

or as fully specified objects:

```json
"ticks": [
  { "value": 180, "label": "180" },
  { "value": 273.15, "label": "273 K / 0 °C" },
  { "value": 320, "label": "320" }
]
```

The image-generation LUT and the legend colour stops should come from the same product definition. Do not maintain two independent palettes.

### Categorical legend

Use categorical legends for cloud type, cloud phase, precipitation class, snow/ice masks and quality flags.

```json
{
  "presentation": {
    "legend": {
      "kind": "categorical",
      "title": "Классификация облачности",
      "categories": [
        { "color": "#000000", "label": "Ясно" },
        { "color": "#D9E6F2", "label": "Низкая облачность" },
        { "color": "#78A8CC", "label": "Средняя облачность" },
        { "color": "#355C9A", "label": "Высокая облачность" },
        { "color": "#8C5DA8", "label": "Тонкая перистая" },
        { "color": "#D84A4A", "label": "Мощная конвективная" },
        { "color": "#646B73", "label": "Не определено" }
      ],
      "notes": [
        "Цвета классов задаются алгоритмом классификации, а не формируются автоматически из RGB-композита."
      ]
    }
  }
}
```

### Explicit composite legend

Automatic analysis is the default, but a scientifically reviewed explanation can replace it:

```json
{
  "presentation": {
    "legend": {
      "kind": "composite",
      "title": "Ночная микрофизика облаков",
      "subtitle": "Условный RGB-композит для качественной интерпретации",
      "notes": [
        "R: разность яркостных температур 12,0–10,8 мкм",
        "G: разность яркостных температур 10,8–3,9 мкм",
        "B: яркостная температура 10,8 мкм, инвертированная",
        "Результирующий цвет не является самостоятельной физической величиной."
      ]
    }
  }
}
```

## Per-product overrides

The `presentation` object is read directly from an instrument composite preset. It does not alter `ImageCompositeCfg`, preserving compatibility with unmodified SatDump 1.2.2 presets.

Supported top-level fields:

```json
{
  "presentation": {
    "enabled": true,
    "title": "Display title",
    "subtitle": "Legend subtitle",
    "branding": "Organisation · SatDump 1.2.2",
    "theme": {
      "panel": "#0E1624",
      "panel_secondary": "#172235",
      "text": "#F3F7FB",
      "muted_text": "#AAB8C8",
      "accent": "#4EC7E8"
    },
    "legend": {}
  }
}
```

Set `"presentation": false` or `"presentation": { "enabled": false }` to suppress the additional output for a specific preset.

## Metadata input contract

The renderer accepts existing SatDump product metadata and also recognises a normalized acquisition block when a recorder or live-processing pipeline provides it:

```json
{
  "acquisition": {
    "pass": {
      "direction": "нисходящий пролёт",
      "max_elevation_deg": 67.2
    },
    "downlink": {
      "center_frequency_hz": 137900000,
      "sample_rate_hz": 240000
    }
  },
  "quality": {
    "score": 94,
    "packet_loss_percent": 0.8,
    "snr_db": 17.4
  }
}
```

The current renderer searches both the product object and the composite metadata returned by `make_composite_from_product()`.

## JSON sidecar

The sidecar uses schema identifier:

```text
satdump.presentation/1
```

It records the exact text and data used to render the header and legend, including colour stops, tick positions, category labels, composite components and explanatory notes. This allows later re-rendering without decoding the raw pass again.

## Development rules

1. Never replace or resize the scientific product to make room for decoration.
2. Never label a generic infrared brightness-temperature product as a retrieved cloud-top temperature unless the retrieval algorithm warrants that name.
3. Never create a scalar colour bar for an RGB product merely because it is colourful.
4. Always describe the component channels of an ambiguous composite.
5. Never invent reception metadata from a satellite name; show it only when the actual session recorded it.
6. Keep visual defaults strict and quiet; reserve accent colours for hierarchy, component markers and status.
7. Treat Russian and English text as first-class UTF-8 content and test both.
