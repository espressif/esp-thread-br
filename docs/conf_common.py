from esp_docs.conf_docs import *  # noqa: F403,F401

languages = ['en']

extensions += ['sphinx_copybutton',
        # Needed as a trigger for running doxygen
        'esp_docs.esp_extensions.dummy_build_system',
        'esp_docs.esp_extensions.run_doxygen',
        ]

# link roles config
github_repo = 'espressif/esp-thread-br'

# context used by sphinx_idf_theme
html_context['github_user'] = 'espressif'
html_context['github_repo'] = 'esp-thread-br'

html_static_path = ['../_static']

# Extra options required by sphinx_idf_theme
project_slug = 'esp-thread-br'

# Contains info used for constructing target and version selector
# Can also be hosted externally, see esp-idf for example
versions_url = '_static/esp_thread_br_version.js'

# Final PDF filename will contains target and version
pdf_file_prefix = u'esp-docs'
