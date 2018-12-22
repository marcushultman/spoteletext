#include "jsp_filter.h"

#include <iostream>
#include <string>
#include "jspextractor.h"

using namespace jsproperty;

std::string data = R"json({
  "device" : {
    "id" : "",
    "is_active" : true,
    "is_private_session" : false,
    "is_restricted" : false,
    "name" : "Living Room TV",
    "type" : "CastVideo",
    "volume_percent" : 100
  },
  "shuffle_state" : false,
  "repeat_state" : "off",
  "timestamp" : 1545507836575,
  "context" : {
    "external_urls" : {
      "spotify" : "https://open.spotify.com/artist/0PFtn5NtBbbUNbU9EAmIWF"
    },
    "href" : "https://api.spotify.com/v1/artists/0PFtn5NtBbbUNbU9EAmIWF",
    "type" : "artist",
    "uri" : "spotify:artist:0PFtn5NtBbbUNbU9EAmIWF"
  },
  "progress_ms" : 72640,
  "item" : {
    "album" : {
      "album_type" : "album",
      "artists" : [ {
        "external_urls" : {
          "spotify" : "https://open.spotify.com/artist/0PFtn5NtBbbUNbU9EAmIWF"
        },
        "href" : "https://api.spotify.com/v1/artists/0PFtn5NtBbbUNbU9EAmIWF",
        "id" : "0PFtn5NtBbbUNbU9EAmIWF",
        "name" : "Toto",
        "type" : "artist",
        "uri" : "spotify:artist:0PFtn5NtBbbUNbU9EAmIWF"
      } ],
      "available_markets" : [ "AD", "AE", "AR", "AT", "AU", "BE", "BG", "BH", "BO", "BR", "CA", "CH", "CL", "CO", "CR", "CY", "CZ", "DE", "DK", "DO", "DZ", "EC", "EE", "EG", "ES", "FI", "FR", "GB", "GR", "GT", "HK", "HN", "HU", "ID", "IE", "IL", "IS", "IT", "JO", "JP", "KW", "LB", "LI", "LT", "LU", "LV", "MA", "MC", "MT", "MX", "MY", "NI", "NL", "NO", "NZ", "OM", "PA", "PE", "PH", "PL", "PS", "PT", "PY", "QA", "RO", "SA", "SE", "SG", "SK", "SV", "TH", "TN", "TR", "TW", "US", "UY", "VN", "ZA" ],
      "external_urls" : {
        "spotify" : "https://open.spotify.com/album/1mnu4hYvdwQgZXcNvtJ3D3"
      },
      "href" : "https://api.spotify.com/v1/albums/1mnu4hYvdwQgZXcNvtJ3D3",
      "id" : "1mnu4hYvdwQgZXcNvtJ3D3",
      "images" : [ {
        "height" : 640,
        "url" : "https://i.scdn.co/image/419366b78e2350c087fcf3674ecea4d5abda7611",
        "width" : 640
      }, {
        "height" : 300,
        "url" : "https://i.scdn.co/image/dedd97ed6f2f78405e0f127f96961b0276cef1bc",
        "width" : 300
      }, {
        "height" : 64,
        "url" : "https://i.scdn.co/image/d6d856563f9ed3d3cc4068a0828a54a7ad4403b3",
        "width" : 64
      } ],
      "name" : "Toto",
      "release_date" : "1978-10-15",
      "release_date_precision" : "day",
      "total_tracks" : 10,
      "type" : "album",
      "uri" : "spotify:album:1mnu4hYvdwQgZXcNvtJ3D3"
    },
    "artists" : [ {
      "external_urls" : {
        "spotify" : "https://open.spotify.com/artist/0PFtn5NtBbbUNbU9EAmIWF"
      },
      "href" : "https://api.spotify.com/v1/artists/0PFtn5NtBbbUNbU9EAmIWF",
      "id" : "0PFtn5NtBbbUNbU9EAmIWF",
      "name" : "Toto",
      "type" : "artist",
      "uri" : "spotify:artist:0PFtn5NtBbbUNbU9EAmIWF"
    } ],
    "available_markets" : [ "AD", "AE", "AR", "AT", "AU", "BE", "BG", "BH", "BO", "BR", "CA", "CH", "CL", "CO", "CR", "CY", "CZ", "DE", "DK", "DO", "DZ", "EC", "EE", "EG", "ES", "FI", "FR", "GB", "GR", "GT", "HK", "HN", "HU", "ID", "IE", "IL", "IS", "IT", "JO", "JP", "KW", "LB", "LI", "LT", "LU", "LV", "MA", "MC", "MT", "MX", "MY", "NI", "NL", "NO", "NZ", "OM", "PA", "PE", "PH", "PL", "PS", "PT", "PY", "QA", "RO", "SA", "SE", "SG", "SK", "SV", "TH", "TN", "TR", "TW", "US", "UY", "VN", "ZA" ],
    "disc_number" : 1,
    "duration_ms" : 235800,
    "explicit" : false,
    "external_ids" : {
      "isrc" : "USSM17800444"
    },
    "external_urls" : {
      "spotify" : "https://open.spotify.com/track/4aVuWgvD0X63hcOCnZtNFA"
    },
    "href" : "https://api.spotify.com/v1/tracks/4aVuWgvD0X63hcOCnZtNFA",
    "id" : "4aVuWgvD0X63hcOCnZtNFA",
    "is_local" : false,
    "name" : "Hold the Line",
    "popularity" : 73,
    "preview_url" : "https://p.scdn.co/mp3-preview/3f451d46def07f436729cd5b48d7d88a7d48dc4d?cid=234d6f04cddd4c3cb92bcd390fd9f4e7",
    "track_number" : 9,
    "type" : "track",
    "uri" : "spotify:track:4aVuWgvD0X63hcOCnZtNFA"
  },
  "currently_playing_type" : "track",
  "is_playing" : true
})json";

int main() {
  extractor context{"href"}, title{"name"}, artist{"name"}, image{"url"}, image_height{"height"};
  filter(data, "\"context\"", 1, 0, context);
  filter(data, "\"item\"", 1, 0, title);
  filter(data, "\"artists\"", 2, 0, artist);
  filter(data, "\"images\"", 3, 2, image);
  filter(data, "\"images\"", 3, 2, image_height);

  assert(*context == "https://api.spotify.com/v1/artists/0PFtn5NtBbbUNbU9EAmIWF");
  assert(*title == "Hold the Line");
  assert(*artist == "Toto");
  assert(*image == "https://i.scdn.co/image/d6d856563f9ed3d3cc4068a0828a54a7ad4403b3");
  assert(*image_height == "64");

  std::cout << "ok" << std::endl;
}
