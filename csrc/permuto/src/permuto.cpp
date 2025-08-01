/** @file   permuto.cpp
 *  @author Jianfei Guo, Shanghai AI Lab
 *  @brief  A re-implementation of the permutohedral encoding.

New features:
- Support half(float16) param dtype
- Support 2 <= n_levels <= 20
- Support n_feats >= 2
- Support different layers using different widths (n_feats)
- Support batched inference with batch inds or batched input

Original: https://github.com/RaduAlexandru/permutohedral_encoding

Citation: 
@inproceedings{rosu2023permutosdf,
	title={PermutoSDF: Fast Multi-View Reconstruction with 
			Implicit Surfaces using Permutohedral Lattices  },
	author={Radu Alexandru Rosu and Sven Behnke},
	booktitle = {IEEE/CVF Conference on Computer Vision and Pattern Recognition (CVPR)},
	year={2023}
}
*/

#ifdef _MSC_VER
#pragma warning(push, 0)
#include <torch/extension.h>
#pragma warning(pop)
#else
#include <torch/extension.h>
#endif

#ifdef snprintf
#undef snprintf
#endif

#include <permuto/permuto.h>

#include <torch/torch.h>
#include <torch/extension.h>

// 生成零和子空间的随机旋转矩阵
torch::Tensor random_rotation_in_zero_sum_subspace_cuda(int dim, int num, torch::Device device, torch::Dtype dtype) {
    // 创建单位矩阵
    torch::Tensor I = torch::eye(dim + 1, device=device, dtype=dtype);
    torch::Tensor ones = torch::ones({dim + 1, 1}, device=device, dtype=dtype);
    auto Q = torch::linalg::qr(I - torch::matmul(ones, ones.transpose(0, 1)) / (dim + 1)).Q;  // QR分解
    Q = Q.slice(1, 0, -1); // [dim+1, dim]

    // 生成 num 个随机旋转矩阵
    torch::Tensor R_sub = torch::randn({num, dim, dim}, device=device, dtype=dtype);  // 随机矩阵
    for (int i = 0; i < num; ++i) {
        auto R = torch::linalg::qr(R_sub[i]).Q; // QR 分解确保正交性
        R_sub[i] = R; // 存储旋转矩阵
    }

    // 批量旋转矩阵乘法：Q @ R @ Q.T
    auto Q_T = Q.transpose(0, 1).unsqueeze(0).expand(num, -1, -1); // [num, dim, dim+1]
    auto Q_exp = Q.unsqueeze(0).expand(num, -1, -1); // [num, dim+1, dim]

    // 执行旋转操作
    return torch::matmul(Q_exp, torch::matmul(R_sub, Q_T));  // 返回旋转后的矩阵
}



PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
#define OPTIONAL_ARGS \
	py::arg("level_random_shifts")=nullptr, \
	py::arg("batch_inds")=nullptr, \
	py::arg("batch_offsets")=nullptr, \
	py::arg("batch_data_size")=nullptr, \
	py::arg("max_level")=nullptr

	m.def("permuto_enc_fwd", &permuto::permuto_enc_fwd, py::arg("meta"), py::arg("positions"), py::arg("lattice_values"), OPTIONAL_ARGS);
	m.def("permuto_enc_bwd", &permuto::permuto_enc_bwd, py::arg("meta"), py::arg("dL_dy"), py::arg("positions"), py::arg("lattice_values"), OPTIONAL_ARGS, py::arg("max_pos_dims"), py::arg("need_input_grad")=nullptr, py::arg("need_param_grad")=nullptr);
	m.def("permuto_enc_bwd_bwd_input", &permuto::permuto_enc_bwd_bwd_input, py::arg("meta"), py::arg("dL_ddLdx"), py::arg("dL_dy"), py::arg("positions"), py::arg("lattice_values"), OPTIONAL_ARGS, py::arg("need_dL_ddLdy")=nullptr, py::arg("need_dL_dparams")=nullptr);
	m.attr("supported_n_input_dims") = py::cast(permuto::supported_n_input_dims);

#undef OPTIONAL_ARGS

	py::class_<permuto::PermutoEncMeta>(m, "PermutoEncMeta")
		.def(
			py::init<const int32_t, const int32_t, const std::vector<double>&, const std::vector<int32_t>& >(), 
			"Create permutohedra encoding meta", 
			py::arg("n_input_dim"), py::arg("hashmap_size"), py::arg("res_list"), py::arg("n_feats_list")
		)
		.def_readonly("level_scales_multidim", &permuto::PermutoEncMeta::level_scales_multidim)
		.def_readonly("level_scales0", &permuto::PermutoEncMeta::level_scales0)
		.def_readonly("level_n_feats", &permuto::PermutoEncMeta::level_n_feats)
		.def_readonly("level_n_params", &permuto::PermutoEncMeta::level_n_params)
		.def_readonly("level_offsets", &permuto::PermutoEncMeta::level_offsets)
		.def_readonly("level_sizes", &permuto::PermutoEncMeta::level_sizes)
		.def_readonly("map_levels", &permuto::PermutoEncMeta::map_levels)
		.def_readonly("map_cnt", &permuto::PermutoEncMeta::map_cnt) 

		.def_readonly("n_levels", &permuto::PermutoEncMeta::n_levels)
		.def_readonly("n_pseudo_levels", &permuto::PermutoEncMeta::n_pseudo_levels)
		.def_readonly("n_feat_per_pseudo_lvl", &permuto::PermutoEncMeta::n_feat_per_pseudo_lvl)
		.def_readonly("n_dims_to_encode", &permuto::PermutoEncMeta::n_dims_to_encode)
		.def_readonly("n_encoded_dims", &permuto::PermutoEncMeta::n_encoded_dims)
		.def_readonly("n_params", &permuto::PermutoEncMeta::n_params)

		;
		
	m.def("random_rotation_in_zero_sum_subspace_cuda", &random_rotation_in_zero_sum_subspace_cuda, "Generate random rotations in zero-sum subspace");

	
}