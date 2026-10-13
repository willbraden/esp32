# Weather data

Where every number on the screen comes from.

## Sub-features

- geolocation: `http://ip-api.com/json/?fields=status,lat,lon,city` (no key, plain HTTP)
- forecast: Open-Meteo `https://api.open-meteo.com/v1/forecast` (no key, TLS with `setInsecure()`)
- current: `temperature_2m`, `weather_code`, and `current.time` (location-local, gives the weekday)
- daily: `temperature_2m_max/min` for today (Hi/Lo)
- next-6h: hourly `precipitation_probability`, `snowfall`, `weather_code` with `forecast_hours=6` (starts at the current hour, crosses midnight)
- units: `temperature_unit=fahrenheit` (`USE_FAHRENHEIT 1`), `timezone=auto`

## How to get to it (user POV)

Automatic. Location follows the network's public IP, so a VPN or a carrier IP can move the forecast to another city.

## Driving it

Check the API directly from the Mac:

```bash
curl -s "https://api.open-meteo.com/v1/forecast?latitude=39.4895&longitude=-104.8447&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&hourly=precipitation_probability,snowfall,weather_code&forecast_hours=6&temperature_unit=fahrenheit&timezone=auto&forecast_days=1" | python3 -m json.tool
```

On device, the `Temp ... precip6h P% (snow) @ DAY time` line is the parsed result. Compare it against the curl output for the same 15-minute slot.

Derived values:
- `precipPct6h` = max hourly probability over the window
- `snow6h` = any hour with snowfall > 0 or a snow weather code (71–77, 85, 86)
- weekday = Sakamoto's method on `current.time`'s date (checked against Python for every day 2000–2100)

## Gotchas

- `current.time` is the start of a 15-minute slot, so "@ 10:15 PM" can be up to 15 minutes old.
- `precipitation_probability` can be null for some hours or models. Parsing defaults to 0.
- ip-api is plain HTTP and rate-limited (45/min). The stress test stays well under that.
- The `pbuf_free` crash fires right after this lookup on about 1 in 3 boots (open). See `known-issues.md`.
