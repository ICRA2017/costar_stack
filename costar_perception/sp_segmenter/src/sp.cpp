#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "sp_segmenter/features.h"

/************************************************************************************************************************************/

spExt::spExt(float ss_)
{
    down_ss = ss_;
    clear();
    
    voxel_resol = 0.005; // 0.005m
    seed_resol = 0.05;    //0.
    color_w = 1.0;
    spatial_w = 0.3;
    normal_w = 0.9;
}

spExt::~spExt()
{
    clear();
}

void spExt::setParams(float voxel_resol_, float seed_resol_, float color_w_, float spatial_w_, float normal_w_)
{
    voxel_resol = voxel_resol_;
    seed_resol = seed_resol_;
    color_w = color_w_;
    spatial_w = spatial_w_;
    normal_w = normal_w_;
}

pcl::PointCloud<PointT>::Ptr spExt::getCloud()
{
    return down_cloud;
}

pcl::PointCloud<PointLT>::Ptr spExt::getLabels()
{
    return label_cloud;
}

IDXSET spExt::getSegsToCloud()
{
    return segs_to_cloud;
}

void spExt::clear()
{
    down_cloud = pcl::PointCloud<PointT>::Ptr (new pcl::PointCloud<PointT>());
    label_cloud = pcl::PointCloud<PointLT>::Ptr (new pcl::PointCloud<PointLT>());
    
    low_segs.clear();
    for( size_t i = 0 ; i < sp_level_idx.size() ; i++ )
        sp_level_idx[i].clear();
    sp_level_idx.clear();
    graph.clear();
    low_seg_num = 0;
    
    segs_to_cloud.clear();
    sp_flags.clear();
}

void spExt::buildOneSPLevel(int level)
{
    if( level < 0 || down_cloud->empty() == true )
    {
        std::cerr << "Input Argument to buildOneSPLevel is wrong OR No cloud available!" << std::endl;
        return;
    }
    else if( level == 0 )
    {
        label_cloud = SPCloud(down_cloud, low_segs, segs_to_cloud, graph, voxel_resol, seed_resol, color_w, spatial_w, normal_w);
        IDXSET idx_0;
        for( size_t j = 0 ; j < low_segs.size() ; j++ ){
            std::vector<int> tmp;
            tmp.push_back(j);
            idx_0.push_back(tmp);
        }
        low_seg_num = idx_0.size();
        sp_level_idx.push_back(idx_0);
        sp_flags.resize(low_seg_num, 1);
    }
    else
    {
        if( sp_level_idx.size() < level || sp_level_idx[level-1].empty() == true )
            buildOneSPLevel(level-1);
        
        std::set< std::vector<int> > new_idxs_set;
        IDXSET new_idxs;
        std::vector<IDXSET>::iterator pre_idx = sp_level_idx.begin() + level - 1;
        for( size_t i = 0 ; i < pre_idx->size() ; i++ )
        {
            std::vector<bool> flags(low_seg_num+1, false);
            bool good = true;
            for(std::vector<int>::const_iterator it = pre_idx->at(i).begin() ; it < pre_idx->at(i).end() ; it++ )
            {
                if( low_segs[*it]->empty() == true || sp_flags[*it] <= 0 )
                {
                    good = false;
                    break;
                }
                flags[*it] = true;
            }
            if( good == false )
                continue;
            
            for(std::vector<int>::const_iterator it = pre_idx->at(i).begin() ; it < pre_idx->at(i).end() ; it++ )
            {
                std::multimap<uint32_t,uint32_t>::const_iterator adj_it;
                for( adj_it = graph.equal_range(*it).first ; adj_it != graph.equal_range(*it).second ; ++adj_it )
                {
                    int adj_label = adj_it->second;
                    if( flags[adj_label] == true || low_segs[adj_label]->empty() == true || sp_flags[adj_label] <= 0 )
                        continue;
                    
                    std::vector<int> tmp_idx = pre_idx->at(i);
                    tmp_idx.push_back(adj_label);
                    std::sort(tmp_idx.begin(), tmp_idx.end());
                    
                    std::set< std::vector<int> >::iterator set_it = new_idxs_set.find(tmp_idx);
                    if( set_it == new_idxs_set.end() )
                    {
                        new_idxs_set.insert(tmp_idx);
                        new_idxs.push_back(tmp_idx);
                    }
                }
            }
        }
        if( sp_level_idx.size() == level )
            sp_level_idx.push_back(new_idxs);
        else
            sp_level_idx[level] = new_idxs;
    }
}

void spExt::LoadPointCloud(const pcl::PointCloud<PointT>::Ptr cloud)
{
    down_cloud = pcl::PointCloud<PointT>::Ptr (new pcl::PointCloud<PointT>());
    if( down_ss > 0 )
    {
        pcl::VoxelGrid<PointT> sor;
        sor.setInputCloud(cloud);
        sor.setLeafSize(down_ss, down_ss, down_ss);
        sor.filter(*down_cloud);
    }
    else
        pcl::copyPointCloud(*cloud, *down_cloud);
}

void spExt::setSPFlags(const std::vector<int> &sp_flags_, bool constrained_flag)
{
    if( sp_level_idx.empty() == true )
    {
        std::cerr << "SP Flags Set Failed! Please call buildOneSPLevel(0) first!" << std::endl;
        return;
    }
    if( sp_flags_.size() != sp_flags.size() )
    {
        std::cerr << "SP Flags Set Failed! sp_flags_.size() != low_seg_num!" << std::endl;
        return;
    }
    if( constrained_flag == true )
        sp_flags = sp_flags_;
    
    IDXSET idx_0;
    for( size_t j = 0 ; j < low_segs.size() ; j++ ){
        if( sp_flags_[j] > 0 )
        {
            std::vector<int> tmp;
            tmp.push_back(j);
            idx_0.push_back(tmp);
        }
    }
    sp_level_idx[0] = idx_0;
    for( size_t ll = 1 ; ll < sp_level_idx.size() ; ll++ )
        buildOneSPLevel(ll);   
}

//void spExt::updateLevel(int level, const IDXSET &in_idx)
//{
//    if( sp_level_idx.size() <= level )
//    {
//        std::cerr << "SP Level Update Failed!" << std::endl;
//        return;
//    }
//    
//    sp_level_idx[level] = in_idx;
//    for( size_t ll = level + 1 ; ll < sp_level_idx.size() ; ll++ )
//        buildOneSPLevel(ll);
//}

IDXSET spExt::getSPIdx(int level)
{
    if( sp_level_idx.size() <= level )
        buildOneSPLevel(level);
    
    return sp_level_idx[level];
}

std::vector<pcl::PointCloud<PointT>::Ptr> spExt::getSPCloud(int level)
{
    if( sp_level_idx.size() <= level )
        buildOneSPLevel(level);
    
    std::vector<IDXSET>::iterator cur_ptr = sp_level_idx.begin() + level;
    std::vector<pcl::PointCloud<PointT>::Ptr> segs(cur_ptr->size());
    for( size_t i = 0 ; i < cur_ptr->size() ; i++ )
    {
        pcl::PointCloud<PointT>::Ptr cur_seg(new pcl::PointCloud<PointT>());
        
        for( std::vector<int>::const_iterator it = cur_ptr->at(i).begin() ; it < cur_ptr->at(i).end() ; it++ )
            cur_seg->insert(cur_seg->end(), low_segs[*it]->begin(), low_segs[*it]->end());
        
        cur_seg->height = 1;
        cur_seg->width = cur_seg->size();    
        
        segs[i] = cur_seg;
    }
    
    return segs;
}


/************************************************************************************************************************************/

spPooler::spPooler()
{
    reset();
}

spPooler::~spPooler()
{
    reset();
}

void spPooler::reset()
{
    sp_num = 0;
    full_cloud = pcl::PointCloud<PointT>::Ptr (new pcl::PointCloud<PointT>());
    
    raw_sp_lab.clear();
    raw_sp_fpfh.clear();
    raw_sp_sift.clear();
    
    raw_color_fea.clear();
    raw_depth_fea.clear();
    raw_data_seg.clear();
    segs_to_cloud.size();
    
    data.cloud = pcl::PointCloud<PointT>::Ptr (new pcl::PointCloud<PointT>());
    data.cloud_normals = pcl::PointCloud<NormalT>::Ptr (new pcl::PointCloud<NormalT>());
    data.img = cv::Mat::zeros(0,0,CV_8UC3);
    data.map2d = cv::Mat::zeros(0,0,CV_32SC1);
    
    segs_label.clear();
    class_responses.clear();
    segs_max_score.clear();
    ext_sp.clear();
}

void spPooler::lightInit(const pcl::PointCloud<PointT>::Ptr cloud, Hier_Pooler& cshot_producer, float radius, float down_ss)
{
    // If not use SIFT pooling!!! Use this light version.
    reset();
    
    pcl::PointCloud<NormalT>::Ptr cloud_normals(new pcl::PointCloud<NormalT>());
    computeNormals(cloud, cloud_normals, radius);
    data = convertPCD(cloud, cloud_normals);
    
    // ext_sp is for superpixel extraction from the segmented point cloud
    ext_sp.setSS(down_ss);
    ext_sp.setParams(0.005, 0.05, 0.5, 0.5, 0.0);   //TODO, from ROS main
    ext_sp.clear();
    ext_sp.LoadPointCloud(cloud);
    data.down_cloud  = ext_sp.getCloud();
    
    std::vector< pcl::PointCloud<PointT>::Ptr > segs = ext_sp.getSPCloud(0);
    segs_to_cloud = ext_sp.getSegsToCloud();
    
    sp_num = segs_to_cloud.size();
    segs_label.resize(sp_num, 1);
    segs_max_score.resize(sp_num, -1000.0);
    class_responses.resize(sp_num);
    
    std::cerr << "CSHOT Extraction..." << std::endl;
    std::vector<cv::Mat> main_fea = cshot_producer.getHierFea(data, 0);
    int depth_len = main_fea[0].cols;
    int color_len = main_fea[1].cols;
    
    raw_color_fea.resize(sp_num);
    raw_depth_fea.resize(sp_num);
    raw_data_seg.resize(sp_num);
    
//    #pragma omp parallel for schedule(dynamic, 1)
    for(size_t i = 0 ; i < sp_num ; i++ )
    {
        size_t cur_seg_size = segs_to_cloud[i].size();
        raw_data_seg[i] = convertPCD(cloud, cloud_normals);
        
        if( cur_seg_size <= 0 )
            continue;
        raw_depth_fea[i] = cv::Mat::zeros(cur_seg_size, depth_len, CV_32FC1);
        raw_color_fea[i] = cv::Mat::zeros(cur_seg_size, color_len, CV_32FC1);
        
        pcl::PointCloud<PointT>::Ptr down_ptr = raw_data_seg[i].down_cloud;
        for( size_t j = 0 ; j < cur_seg_size; j++ )
        {
            int cur_idx = segs_to_cloud[i][j];
            main_fea[0].row(cur_idx).copyTo(raw_depth_fea[i].row(j));
            main_fea[1].row(cur_idx).copyTo(raw_color_fea[i].row(j));
            
            down_ptr->push_back(data.down_cloud->at(cur_idx));
        }
    }   
    
}


void spPooler::init(const pcl::PointCloud<PointT>::Ptr full_cloud_, Hier_Pooler& cshot_producer, float radius, float down_ss)
{
    reset();
    full_cloud = full_cloud_;
    if( full_cloud->isOrganized() == false )
    {
        std::cerr << "data.cloud->isOrganized() == false" << std::endl;
        exit(0);
    }
    pcl::PointCloud<PointT>::Ptr cloud = refineScene(full_cloud);
    
    pcl::PointCloud<NormalT>::Ptr cloud_normals(new pcl::PointCloud<NormalT>());
    computeNormals(cloud, cloud_normals, radius);
    data = convertPCD(cloud, cloud_normals);
    data.img = getFullImage(full_cloud);
    
    ext_sp.setSS(down_ss);
    ext_sp.clear();
    ext_sp.LoadPointCloud(cloud);
    data.down_cloud  = ext_sp.getCloud();
    
    std::vector< pcl::PointCloud<PointT>::Ptr > segs = ext_sp.getSPCloud(0);
    
//    for(int i = 0 ; i < segs.size() ; i++ )
//    {
//        std::vector< pcl::PointCloud<PointT>::Ptr > big_segs = ext_sp.getSPCloud(i);
//        std::cerr << big_segs.size() << std::endl;
//    }
//    std::cin.get();
    segs_to_cloud = ext_sp.getSegsToCloud();
    
    sp_num = segs_to_cloud.size();
    segs_label.resize(sp_num, 1);
    segs_max_score.resize(sp_num, -1000.0);
    class_responses.resize(sp_num);
    
    std::vector<cv::Mat> main_fea = cshot_producer.getHierFea(data, 0);
    int depth_len = main_fea[0].cols;
    int color_len = main_fea[1].cols;
    
    raw_color_fea.resize(sp_num);
    raw_depth_fea.resize(sp_num);
    raw_data_seg.resize(sp_num);
//    std::vector<int> tmp_idx(1);
//    std::vector<float> sqr_dist(1);
    
    #pragma omp parallel for schedule(dynamic, 1)
    for(size_t i = 0 ; i < sp_num ; i++ )
    {
        size_t cur_seg_size = segs_to_cloud[i].size();
        raw_data_seg[i] = convertPCD(cloud, cloud_normals);
        
        if( cur_seg_size <= 0 )
            continue;
        raw_depth_fea[i] = cv::Mat::zeros(cur_seg_size, depth_len, CV_32FC1);
        raw_color_fea[i] = cv::Mat::zeros(cur_seg_size, color_len, CV_32FC1);
        
//        raw_data_seg[i].img = data.img;
//        raw_data_seg[i]._3d2d = cv::Mat::zeros(cur_seg_size, 2, CV_32SC1);
//        cv::Mat idx3d_2d = raw_data_seg[i]._3d2d;
        pcl::PointCloud<PointT>::Ptr down_ptr = raw_data_seg[i].down_cloud;
//        raw_data_seg[i].map2d = cv::Mat::zeros(img_h, img_w, CV_32SC1);
        for( size_t j = 0 ; j < cur_seg_size; j++ )
        {
            int cur_idx = segs_to_cloud[i][j];
            main_fea[0].row(cur_idx).copyTo(raw_depth_fea[i].row(j));
            main_fea[1].row(cur_idx).copyTo(raw_color_fea[i].row(j));
            
//            int nres = tree->nearestKSearch(down_cloud->at(cur_idx), 1, tmp_idx, sqr_dist);
//            int row = tmp_idx[0] / width;
//            int col = tmp_idx[0] % width;
//            idx3d_2d.at<int>(j, 0) = col;
//            idx3d_2d.at<int>(j, 1) = row;
            
            down_ptr->push_back(data.down_cloud->at(cur_idx));
        }
//        std::cerr << raw_data_seg[i].down_cloud->size() << std::endl;
    }   
    
}


void spPooler::build_SP_LAB(const std::vector<boost::shared_ptr<Pooler_L0> >& lab_pooler_set, bool max_pool_flag)
{
    int pooler_num = lab_pooler_set.size();
    raw_sp_lab.clear();
    raw_sp_lab.resize(sp_num);
    
    #pragma omp parallel for schedule(dynamic, 1)
    for(size_t i = 0 ; i < sp_num ; i++ )
    {
        if( raw_data_seg[i].down_cloud->empty() == true )
            continue;
        
        PreCloud(raw_data_seg[i], -1, true);
        for( int k = 1 ; k < pooler_num ; k++ )
        {
            std::vector<cv::Mat> temp_fea1 = lab_pooler_set[k]->PoolOneDomain_Raw(raw_data_seg[i].rgb, raw_depth_fea[i], 1, max_pool_flag);
            std::vector<cv::Mat> temp_fea2 = lab_pooler_set[k]->PoolOneDomain_Raw(raw_data_seg[i].rgb, raw_color_fea[i], 1, max_pool_flag);
            raw_sp_lab[i].insert(raw_sp_lab[i].end(), temp_fea1.begin(), temp_fea1.end());
            raw_sp_lab[i].insert(raw_sp_lab[i].end(), temp_fea2.begin(), temp_fea2.end());
        }
    }
}

void spPooler::build_SP_FPFH(const std::vector< boost::shared_ptr<Pooler_L0> > &fpfh_pooler_set, float radius, bool max_pool_flag)
{
//    cv::Mat fpfh = fpfh_cloud(data.cloud, data.down_cloud, data.cloud_normals, radius, true);
    int pooler_num = fpfh_pooler_set.size();
    raw_sp_fpfh.clear();
    raw_sp_fpfh.resize(sp_num);
    
//    int count = 0;
    #pragma omp parallel for schedule(dynamic, 1)
    for(size_t i = 0 ; i < sp_num ; i++ )
    {
        if( segs_label[i] <= 0 || raw_data_seg[i].down_cloud->empty() == true )
            continue;
//        count++;
//        size_t cur_seg_size = segs_to_cloud[i].size();
//        cv::Mat cur_fpfh = cv::Mat::zeros(cur_seg_size, fpfh.cols, CV_32FC1);
//        for( size_t j = 0 ; j < cur_seg_size; j++ )
//            fpfh.row(segs_to_cloud[i][j]).copyTo(cur_fpfh.row(j));
        cv::Mat cur_fpfh = fpfh_cloud(raw_data_seg[i].cloud, raw_data_seg[i].down_cloud, raw_data_seg[i].cloud_normals, radius, true);
                
        for( int k = 1 ; k < pooler_num ; k++ )
        {
            std::vector<cv::Mat> temp_fea1 = fpfh_pooler_set[k]->PoolOneDomain_Raw(cur_fpfh, raw_depth_fea[i], 2, max_pool_flag);
            std::vector<cv::Mat> temp_fea2 = fpfh_pooler_set[k]->PoolOneDomain_Raw(cur_fpfh, raw_color_fea[i], 2, max_pool_flag);
            raw_sp_fpfh[i].insert(raw_sp_fpfh[i].end(), temp_fea1.begin(), temp_fea1.end());
            raw_sp_fpfh[i].insert(raw_sp_fpfh[i].end(), temp_fea2.begin(), temp_fea2.end());
        }
    }
//    std::cerr << count << " " << sp_num << std::endl;
}

void spPooler::build_SP_SIFT(const std::vector< boost::shared_ptr<Pooler_L0> > &sift_pooler_set, Hier_Pooler &cshot_producer, const std::vector<cv::SiftFeatureDetector*> &sift_det_vec, bool max_pool_flag)
{
    // cv::SiftFeatureDetector *sift_det = new cv::SiftFeatureDetector(
    //    0, // nFeatures
    //    4, // nOctaveLayers
    //    -10000, // contrastThreshold 
    //    100000, //edgeThreshold
    //    sigma//sigma
    //    );
    
    cv::SiftDescriptorExtractor * sift_ext = new cv::SiftDescriptorExtractor();
    
    pcl::PointCloud<PointLT>::Ptr label_cloud = ext_sp.getLabels();
    cv::Mat cur_rgb = data.img;
    int height = full_cloud->height;
    int width = full_cloud->width;
    
    cv::Mat cur_gray(cur_rgb.size(), CV_8UC1);
    cv::cvtColor(cur_rgb, cur_gray, CV_BGR2GRAY);
    
    pcl::search::KdTree<PointT>::Ptr tree (new pcl::search::KdTree<PointT>);
    tree->setInputCloud(data.down_cloud);
    
    std::vector<cv::KeyPoint> sift_keys = extSIFTKeys(cur_rgb, sift_det_vec);
    // std::vector<cv::KeyPoint> sift_keys;
    // sift_det->detect(cur_gray, sift_keys);
    
    std::vector<MulInfoT> data_set(sp_num);
    std::vector<bool> active_flag(sp_num, false);
    std::vector< std::vector<cv::KeyPoint> > in_sift_keys(sp_num);
    
    std::vector<int> temp_ind(1);
    std::vector<float> temp_dist(1);
    for( size_t k = 0 ; k < sift_keys.size() ; k++ )
    {
        int row = round(sift_keys[k].pt.y);
        int col = round(sift_keys[k].pt.x);
        row = row >= height? height : row;
        col = col >= width? width : col;
        
        int tmp_idx = row*width + col;
        // std::cerr << sift_keys[k].pt.y << " " << sift_keys[k].pt.x << " " << sift_keys[k].angle << " " << sift_keys[k].response << std::endl;
        if( pcl_isfinite(full_cloud->at(tmp_idx).z) == false)
            continue;
        
        tree->nearestKSearch(full_cloud->at(tmp_idx), 1, temp_ind, temp_dist);
        if(temp_dist[0] > 0.02)
            continue;
        
        uint32_t label = label_cloud->at(temp_ind[0]).label;
        if( segs_label[label] > 0 )
        {
            if( active_flag[label] == false )
            {
                data_set[label]= convertPCD(data.cloud, data.cloud_normals);
                active_flag[label] = true;
            }
            data_set[label].down_cloud->push_back(full_cloud->at(tmp_idx));
            in_sift_keys[label].push_back(sift_keys[k]);
        }
    }
    
    raw_sp_sift.clear();
    raw_sp_sift.resize(sp_num);
    
    // int count = 0;
    #pragma omp parallel for schedule(dynamic, 1)
    for( size_t k = 0 ; k < sp_num ; k++ )
    {
        if( in_sift_keys[k].empty() == true )
            continue;
        
        // count++;
        cv::Mat cur_sift_descr;
        sift_ext->compute(cur_gray, in_sift_keys[k], cur_sift_descr);
        for(int r = 0 ; r < cur_sift_descr.rows ; r++ )
            cv::normalize(cur_sift_descr.row(r), cur_sift_descr.row(r));

        std::vector<cv::Mat> main_fea = cshot_producer.getHierFea(data_set[k], 0);
        for( size_t j = 1 ; j < sift_pooler_set.size() ; j++ )
    	{
            std::vector<cv::Mat> temp_fea1 = sift_pooler_set[j]->PoolOneDomain_Raw(cur_sift_descr, main_fea[0], 2, max_pool_flag);
            std::vector<cv::Mat> temp_fea2 = sift_pooler_set[j]->PoolOneDomain_Raw(cur_sift_descr, main_fea[1], 2, max_pool_flag);
            raw_sp_sift[k].insert(raw_sp_sift[k].end(), temp_fea1.begin(), temp_fea1.end());
            raw_sp_sift[k].insert(raw_sp_sift[k].end(), temp_fea2.begin(), temp_fea2.end());
    	}
        
    }
    // std::cerr << count << " " << sp_num << std::endl;
    delete sift_ext;
}

pcl::PointCloud<PointT>::Ptr spPooler::refineScene(const pcl::PointCloud<PointT>::Ptr scene)
{
    pcl::PointCloud<PointT>::Ptr tmp_cloud(new pcl::PointCloud<PointT>());
    std::vector<int> idx_ff;
    pcl::removeNaNFromPointCloud(*scene, *tmp_cloud, idx_ff);
    
    pcl::PassThrough<PointT> pass;
    pass.setInputCloud (tmp_cloud);
    pass.setFilterFieldName ("z");
    pass.setFilterLimits (0.1, 1.5);
    //pass.setFilterLimitsNegative (true);
    pcl::PointCloud<PointT>::Ptr cloud(new pcl::PointCloud<PointT>());
    pass.filter (*cloud);
    
    return cloud;
}

std::vector<cv::Mat> spPooler::getSPFea(const IDXSET &idx_set, bool max_pool, bool normalized)
{
    int num = idx_set.size();
    std::vector<cv::Mat> final_fea;
    
    if( raw_sp_lab.empty() == false )
    {
        std::vector<cv::Mat> lab_fea = combineRaw(raw_sp_lab, idx_set, max_pool, normalized);
        final_fea = lab_fea;
    }
    if( raw_sp_fpfh.empty() == false )
    {
        std::vector<cv::Mat> fpfh_fea = combineRaw(raw_sp_fpfh, idx_set, max_pool, normalized);
        for(int i = 0 ; i < num ; i++ )
            cv::hconcat(final_fea[i], fpfh_fea[i], final_fea[i]);
    }
    if( raw_sp_sift.empty() == false )
    {
        std::vector<cv::Mat> sift_fea = combineRaw(raw_sp_sift, idx_set, max_pool, normalized);
        for(int i = 0 ; i < num ; i++ )
            cv::hconcat(final_fea[i], sift_fea[i], final_fea[i]);
    }
    return final_fea;
}

std::vector<cv::Mat> spPooler::sampleSPFea(const int level, int sample_num, bool max_pool, bool normalized)
{
    IDXSET idx_set;
    if( sample_num > 0 )
    {
        IDXSET tmp_set = ext_sp.getSPIdx(level);
        if( sample_num < (int)tmp_set.size() )
        {
            std::vector<size_t> rand_idx;
            GenRandSeq(rand_idx, tmp_set.size());
            for(int i = 0 ; i < sample_num ; i++ )
                idx_set.push_back(tmp_set[rand_idx[i]]);
        }
        else
            idx_set = tmp_set;
    }
    else
        idx_set = ext_sp.getSPIdx(level);
    
    std::vector<cv::Mat> final_fea = getSPFea(idx_set, max_pool, normalized);
    return final_fea;
}

std::vector<cv::Mat> spPooler::combineRaw(const std::vector< std::vector<cv::Mat> > &raw_set, const IDXSET &idx_set, bool max_pool, bool normalized)
{
    std::vector<cv::Mat> fea_set(idx_set.size());
    
    int pool_num = -1, dim_per_pool = -1;
    for( size_t i = 0 ; i < raw_set.size() ; i++ )
    {
        if( raw_set[i].size() > 0 )
        {
            pool_num = raw_set[i].size();
            dim_per_pool = raw_set[i][0].cols;
            break;
        }
    }
    
    if( pool_num < 0 || dim_per_pool <= 0)
    {
        std::cerr << "Error in combineRaw()!" << std::endl;
        return fea_set;
    }
    
    for( size_t i = 0 ; i < idx_set.size() ; i++ ){
        std::vector<cv::Mat> cur_fea;
        for( std::vector<int>::const_iterator it = idx_set[i].begin() ; it < idx_set[i].end() ; it++ )
        {
            if( raw_set[*it].empty() == true )
                continue;
            if( cur_fea.empty() == true )
            {
                cur_fea.resize(pool_num);
                for( int j = 0 ; j < pool_num ; j++ )
                    cur_fea[j] = raw_set[*it][j].clone();
            }
            else
            {
                for( int j = 0 ; j < pool_num ; j++ )
                {
                    if( max_pool == false )
                        cur_fea[j] += raw_set[*it][j];
                    else 
                        MaxOP(cur_fea[j], raw_set[*it][j]);
                }
            }
        }
        cv::Mat final_fea;
        if( cur_fea.empty() == false )
        {
            if( normalized )
            {
                for( int j = 0 ; j < pool_num ; j++ )
                    cv::normalize(cur_fea[j], cur_fea[j], 1.0, 0.0, cv::NORM_L2);
            }
            cv::hconcat(cur_fea, final_fea);
        }
        else
            final_fea = cv::Mat::zeros(1, pool_num*dim_per_pool, CV_32FC1);
        
        fea_set[i] = final_fea;
    }
    return fea_set;
}

std::vector<cv::Mat> spPooler::gethardNegtive(const model *cur_model, int level, bool max_pool)
{
    int model_num = cur_model->nr_class;
    IDXSET idx_set = ext_sp.getSPIdx(level);
//    std::vector<cv::Mat> sp_fea = getSPFea(idx_set, max_pool);
    
    feature_node bias_term;
    bias_term.index = cur_model->nr_feature;
    bias_term.value = cur_model->bias;
    feature_node end_node;
    end_node.index = -1;
    end_node.value = 0;
    
    std::vector<cv::Mat> hard_negative_vec;
    int num = idx_set.size();
    for( int j = 0 ; j < num ; j++ )
    {
        IDXSET tmp;
        tmp.push_back(idx_set[j]);
        std::vector<cv::Mat> tmp_fea = getSPFea(tmp, max_pool); 
        if( tmp_fea.empty() == true )
            continue;
        
        if( tmp_fea[0].cols != cur_model->nr_feature - 1)
        {
            std::cerr << "sp_fea[j].cols != cur_model->nr_feature - 1" << std::endl;
            exit(0);
        }
        sparseVec sparse_fea;
        CvMatToFeatureNode(tmp_fea[0], sparse_fea);
        sparse_fea.push_back(bias_term);
        sparse_fea.push_back(end_node);
        
        feature_node *cur_fea = new feature_node[sparse_fea.size()];
        std::copy(sparse_fea.begin(), sparse_fea.end(), cur_fea);
        
        double *dec_values = new double[cur_model->nr_class];
        double tmp_label = predict_values(cur_model, cur_fea, dec_values);
        int cur_label = model_num <= 2 ? floor(tmp_label+0.0001-1) : floor(tmp_label+0.0001);
//        float cur_score = model_num <= 2 ? fabs(dec_values[0]) : dec_values[cur_label-1];
        
        if( cur_label >= 1 )
            hard_negative_vec.push_back(tmp_fea[0]);
        
        delete[] cur_fea;
        delete[] dec_values;
    }
//    std::cerr << "Hard Num: " << hard_negative_vec.size() << std::endl;
    
    return hard_negative_vec;
}

void spPooler::InputSemantics(const model *cur_model, int level, bool reset, bool max_pool)
{
    int model_num = cur_model->nr_class;
    if( class_responses.empty() == true )
    {
        std::cerr << "class_responses.empty() == true" << std::endl;
        exit(0);
    }
    if( class_responses[0].empty() == true || reset )
    {
        for(int i = 0 ; i < class_responses.size() ; i++ )
        {
            class_responses[i].clear();
            class_responses[i].resize(model_num+1, -1000.0);
        }
        if( reset )
        {
            segs_label.clear();
            segs_max_score.clear();
            segs_label.resize(sp_num, 0);
            segs_max_score.resize(sp_num, -1000.0);
        }
    }
    
    IDXSET idx_set = ext_sp.getSPIdx(level);
//    std::vector<cv::Mat> sp_fea = getSPFea(idx_set, max_pool);
    
    feature_node bias_term;
    bias_term.index = cur_model->nr_feature;
    bias_term.value = cur_model->bias;
    feature_node end_node;
    end_node.index = -1;
    end_node.value = 0;
    
//    int num = sp_fea.size();
    int num = idx_set.size();
    for( int j = 0 ; j < num ; j++ )
    {
        IDXSET tmp;
        tmp.push_back(idx_set[j]);
        std::vector<cv::Mat> tmp_fea = getSPFea(tmp, max_pool); 
        if( tmp_fea.empty() == true )
            continue;
        
        if( tmp_fea[0].cols != cur_model->nr_feature - 1)
        {
            std::cerr << "sp_fea[j].cols != cur_model->nr_feature - 1" << std::endl;
            exit(0);
        }
        sparseVec sparse_fea;
        CvMatToFeatureNode(tmp_fea[0], sparse_fea);
        sparse_fea.push_back(bias_term);
        sparse_fea.push_back(end_node);
        
        feature_node *cur_fea = new feature_node[sparse_fea.size()];
        std::copy(sparse_fea.begin(), sparse_fea.end(), cur_fea);
        
        double *dec_values = new double[cur_model->nr_class];
        double tmp_label = predict_values(cur_model, cur_fea, dec_values);
        int cur_label = model_num <= 2 ? floor(tmp_label+0.0001-1) : floor(tmp_label+0.0001-1);
        float cur_score = model_num <= 2 ? fabs(dec_values[0]) : dec_values[cur_label-1];
        
        for( std::vector<int>::iterator it = idx_set[j].begin() ; it < idx_set[j].end() ; it++ ){
            if( class_responses[*it][cur_label] < cur_score )
            {
                class_responses[*it][cur_label] = cur_score;
                if( segs_max_score[*it] < cur_score )
                {
                    segs_max_score[*it] = cur_score;
                    segs_label[*it] = cur_label;
                }
            }
        }
        delete[] cur_fea;
        delete[] dec_values;
    }
    
}

pcl::PointCloud<PointLT>::Ptr spPooler::getSemanticLabels()
{
    pcl::PointCloud<PointLT>::Ptr label_cloud(new pcl::PointCloud<PointLT>());
    for(size_t i = 0 ; i < sp_num ; i++ )
    {
        if( segs_label[i] > 0 )
        {
            size_t cur_seg_size = segs_to_cloud[i].size();
            for( size_t j = 0 ; j < cur_seg_size; j++ )
            {
                int cur_idx = segs_to_cloud[i][j];
                PointT *ptr = &data.down_cloud->at(cur_idx);
                PointLT pt;
                pt.x = ptr->x;
                pt.y = ptr->y;
                pt.z = ptr->z;
                pt.label = segs_label[i];
                label_cloud->push_back(pt);
            }
        }
    }   
    
    return label_cloud;
}

void spPooler::extractForeground(bool constrained_flag)
{
    ext_sp.setSPFlags(segs_label, constrained_flag);
}
