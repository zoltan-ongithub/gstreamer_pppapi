{
  'targets': [
   {
      'target_name': 'ppapi_gstreamer',
      'type': 'shared_library',
      'dependencies': [
        '<(DEPTH)/ppapi/ppapi.gyp:ppapi_cpp',
        '<(DEPTH)/ppapi/ppapi.gyp:ppapi_gles2',
      ],
      'include_dirs': [
        '<(DEPTH)/ppapi/lib/gl/include',
      ],

      'cflags': [
      '<!@(<(pkg-config) --cflags gstreamer-1.0)',
      ],
      'sources': [
        'gstreamer/ppapi_gstreamer.cc',
        'gstreamer/video_decoder_gstreamer.cc',
        'gstreamer/video_decoder_gstreamer.h',
#        'gstreamer/gstreamer_player_hole.cc',
#        'gstreamer/gstreamer_player_hole.h',
      ],
      'link_settings': {
            'ldflags': [
              '<!@(<(pkg-config) --libs-only-L --libs-only-other gstreamer-1.0)',
            ],
            'libraries': [
              '<!@(<(pkg-config) --libs-only-l gstreamer-1.0)',
            ],
       },
       'variables': {
            'pkg-config': 'pkg-config',
       },

    },
  ],
}
