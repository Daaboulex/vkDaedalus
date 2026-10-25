data_dir = get_option('datadir')

vulkan_layer_dir = join_paths(data_dir, 'vulkan', 'implicit_layer.d')

configure_file(
    input : 'vkdaedalus.json.in',
    output : 'vkdaedalus.json',
    configuration : {
        'ld_lib_dir_vkdaedalus' : ld_lib_dir_vkdaedalus,
        'library_arch' : library_arch,
        'api_version' : layer_api_version,
    },
    install : true,
    install_dir : vulkan_layer_dir,
)

install_data('vkdaedalus.conf', install_dir : join_paths(data_dir, 'vkdaedalus'))
install_data('order-layers/vkdaedalus_before_lsfgvk.json', 'order-layers/lsfgvk_before_vkdaedalus.json',
    install_dir : join_paths(data_dir, 'vkdaedalus', 'order-layers'))
