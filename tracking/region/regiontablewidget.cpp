#include <QFileDialog>
#include <QContextMenuEvent>
#include <QMessageBox>
#include <QClipboard>
#include <QSettings>
#include "regiontablewidget.h"
#include "tracking/tracking_window.h"
#include "tracking_static_link.h"
#include "qcolorcombobox.h"
#include "ui_tracking_window.h"
#include "mapping/atlas.hpp"
#include "mapping/fa_template.hpp"
#include "opengl/glwidget.h"
#include "manual_alignment.h"
extern std::vector<atlas> atlas_list;
extern fa_template fa_template_imp;

QColor ROIColor[15] =
{
    Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::magenta, Qt::cyan,  Qt::gray,
    Qt::darkRed,Qt::darkGreen, Qt::darkBlue, Qt::darkYellow,  Qt::darkMagenta, Qt::darkCyan,
    Qt::darkGray, Qt::lightGray
};

QWidget *ImageDelegate::createEditor(QWidget *parent,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    if (index.column() == 1)
    {
        QComboBox *comboBox = new QComboBox(parent);
        comboBox->addItem("ROI");
        comboBox->addItem("ROA");
        comboBox->addItem("End");
        comboBox->addItem("Seed");
        connect(comboBox, SIGNAL(activated(int)), this, SLOT(emitCommitData()));
        return comboBox;
    }
    else if (index.column() == 2)
    {
        QColorToolButton* sd = new QColorToolButton(parent);
        connect(sd, SIGNAL(clicked()), this, SLOT(emitCommitData()));
        return sd;
    }
    else
        return QItemDelegate::createEditor(parent,option,index);

}

void ImageDelegate::setEditorData(QWidget *editor,
                                  const QModelIndex &index) const
{

    if (index.column() == 1)
        ((QComboBox*)editor)->setCurrentIndex(index.model()->data(index).toString().toInt());
    else
        if (index.column() == 2)
        {
            image::rgb_color color((unsigned int)(index.data(Qt::UserRole).toInt()));
            ((QColorToolButton*)editor)->setColor(
                QColor(color.r,color.g,color.b,color.a));
        }
        else
            return QItemDelegate::setEditorData(editor,index);
}

void ImageDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                 const QModelIndex &index) const
{
    if (index.column() == 1)
        model->setData(index,QString::number(((QComboBox*)editor)->currentIndex()));
    else
        if (index.column() == 2)
            model->setData(index,(int)(((QColorToolButton*)editor)->color().rgba()),Qt::UserRole);
        else
            QItemDelegate::setModelData(editor,model,index);
}

void ImageDelegate::emitCommitData()
{
    emit commitData(qobject_cast<QWidget *>(sender()));
}


RegionTableWidget::RegionTableWidget(tracking_window& cur_tracking_window_,QWidget *parent):
        QTableWidget(parent),cur_tracking_window(cur_tracking_window_),regions_index(0)
{
    setColumnCount(3);
    setColumnWidth(0,140);
    setColumnWidth(1,60);
    setColumnWidth(2,40);

    QStringList header;
    header << "Name" << "Type" << "Color";
    setHorizontalHeaderLabels(header);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setItemDelegate(new ImageDelegate(this));

    QObject::connect(this,SIGNAL(cellClicked(int,int)),this,SLOT(check_check_status(int,int)));
    QObject::connect(this,SIGNAL(itemChanged(QTableWidgetItem*)),this,SLOT(updateRegions(QTableWidgetItem*)));

}



RegionTableWidget::~RegionTableWidget(void)
{
}

void RegionTableWidget::contextMenuEvent ( QContextMenuEvent * event )
{
    if (event->reason() == QContextMenuEvent::Mouse)
    {
        cur_tracking_window.ui->menuRegions->popup(event->globalPos());
    }
}

void RegionTableWidget::updateRegions(QTableWidgetItem* item)
{
    if (item->column() == 1)
        regions[item->row()].regions_feature = item->text().toInt();
    else
        if (item->column() == 2)
        {
            regions[item->row()].show_region.color = item->data(Qt::UserRole).toInt();
            emit need_update();
        }
}

QColor RegionTableWidget::currentRowColor(void)
{
    return (unsigned int)regions[currentRow()].show_region.color;
}

void RegionTableWidget::add_region(QString name,unsigned char feature,int color)
{
    if(color == 0)
    {
        color = (int)ROIColor[regions_index].rgb();
        if (feature != none_roi_id)
        {
            ++regions_index;
            if (regions_index >= 15)
                regions_index = 0;
        }
    }
    regions.push_back(new ROIRegion(cur_tracking_window.slice.geometry,cur_tracking_window.slice.voxel_size));

    setRowCount(regions.size());

    // the name item
    QTableWidgetItem *item0 = new QTableWidgetItem(name);

    setItem(regions.size()-1, 0, item0);

    QTableWidgetItem *item1 = new QTableWidgetItem(QString::number((int)feature));
    QTableWidgetItem *item2 = new QTableWidgetItem();

    setItem(regions.size()-1, 1, item1);
    item1->setData(Qt::ForegroundRole,QBrush(Qt::white));
    setItem(regions.size()-1, 2, item2);
    item2->setData(Qt::ForegroundRole,QBrush(Qt::white));
    item2->setData(Qt::UserRole,color);
    setRowHeight(regions.size()-1,22);


    openPersistentEditor(item1);
    openPersistentEditor(item2);
    item0->setCheckState(Qt::Checked);

    setCurrentCell(regions.size()-1,0);

}
void RegionTableWidget::check_check_status(int row, int col)
{
    if (col != 0)
        return;
    if (item(row,0)->checkState() == Qt::Checked)
    {
        if (item(row,0)->data(Qt::ForegroundRole) == QBrush(Qt::gray))
        {
            item(row,0)->setData(Qt::ForegroundRole,QBrush(Qt::black));
            emit need_update();
        }
    }
    else
    {
        if (item(row,0)->data(Qt::ForegroundRole) != QBrush(Qt::gray))
        {
            item(row,0)->setData(Qt::ForegroundRole,QBrush(Qt::gray));
            emit need_update();
        }
    }
}

void RegionTableWidget::draw_region(QImage& qimage)
{
    int X, Y, Z;
    for (unsigned int roi_index = 0;roi_index < regions.size();++roi_index)
    {
        if (item(roi_index,0)->checkState() != Qt::Checked)
            continue;
        unsigned int cur_color = regions[roi_index].show_region.color;
        for (unsigned int index = 0;index < regions[roi_index].size();++index)
        {
            regions[roi_index].getSlicePosition(&cur_tracking_window.slice, index, X, Y, Z);
            if (cur_tracking_window.slice.slice_pos[cur_tracking_window.slice.cur_dim] != Z ||
                    X < 0 || Y < 0 || X >= qimage.width() || Y >= qimage.height())
                continue;
            qimage.setPixel(X,Y,(unsigned int)qimage.pixel(X,Y) | cur_color);
        }
    }
}
void RegionTableWidget::draw_mosaic_region(QImage& qimage,unsigned int mosaic_size,unsigned int skip)
{
    image::geometry<3> geo = cur_tracking_window.slice.geometry;
    unsigned int slice_number = geo[2] >> skip;
    std::vector<int> shift_x(slice_number),shift_y(slice_number);
    for(unsigned int z = 0;z < slice_number;++z)
    {
        shift_x[z] = geo[0]*(z%mosaic_size);
        shift_y[z] = geo[1]*(z/mosaic_size);
    }

    for (unsigned int roi_index = 0;roi_index < regions.size();++roi_index)
    {
        if (item(roi_index,0)->checkState() != Qt::Checked)
            continue;
        unsigned int cur_color = regions[roi_index].show_region.color;
        for (unsigned int index = 0;index < regions[roi_index].size();++index)
        {
            int X = regions[roi_index].get()[index][0];
            int Y = regions[roi_index].get()[index][1];
            int Z = regions[roi_index].get()[index][2];
            if(Z != ((Z >> skip) << skip))
                continue;
            X += shift_x[Z >> skip];
            Y += shift_y[Z >> skip];
            if(X >= qimage.width() || Y >= qimage.height())
                continue;
            qimage.setPixel(X,Y,(unsigned int)qimage.pixel(X,Y) | cur_color);
        }
    }
}

void RegionTableWidget::new_region(void)
{
    add_region("New Region",roi_id);
}
bool RegionTableWidget::load_multiple_roi_nii(QString file_name)
{
    gz_nifti header;
    if (!header.load_from_file(file_name.toLocal8Bit().begin()))
        return false;
    image::basic_image<unsigned int, 3> from;
    header >> from;
    std::vector<unsigned char> value_map(std::numeric_limits<unsigned short>::max());
    unsigned int max_value = 0;
    for (image::pixel_index<3>index; index.valid(from.geometry());index.next(from.geometry()))
    {
        value_map[(unsigned short)from[index.index()]] = 1;
        max_value = std::max<unsigned short>(from[index.index()],max_value);
    }
    value_map.resize(max_value+1);

    unsigned short region_count = std::accumulate(value_map.begin(),value_map.end(),(unsigned short)0);
    bool multiple_roi = region_count > 2;
    if(region_count > 2)
    {
        QMessageBox msgBox;
        msgBox.setText("Load multiple ROIs in the nifti file?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        int rec = msgBox.exec();
        if(rec == QMessageBox::No)
            multiple_roi = false;
    }

    std::map<short,std::string> label_map;
    if(multiple_roi)
    {
        QString label_file = QFileInfo(file_name).absolutePath()+"/"+QFileInfo(file_name).baseName()+".txt";
        if(!QFileInfo(label_file).exists())
        {
            QMessageBox msgBox;
            msgBox.setText("Has label file (*.txt)?");
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            msgBox.setDefaultButton(QMessageBox::Yes);
            int rec = msgBox.exec();
            if(rec == QMessageBox::Yes)
                label_file = QFileDialog::getOpenFileName(
                                       this,
                                       "Label file",
                                       QFileInfo(file_name).absolutePath(),
                                       "Text files (*.txt)" );
        }
        if(QFileInfo(label_file).exists())
        {
            std::ifstream in(label_file.toLocal8Bit().begin());
            if(in)
            {
                std::string line,txt;
                while(std::getline(in,line))
                {
                    if(line.empty() || line[0] == '#')
                        continue;
                    std::istringstream read_line(line);
                    short num = 0;
                    read_line >> num >> txt;
                    label_map[num] = txt;
                }
            }
        }
    }

    std::vector<float> convert;

    // searching for T1/T2 mappings
    for(unsigned int index = 0;index < cur_tracking_window.glWidget->other_slices.size();++index)
    {
        if(from.geometry() == cur_tracking_window.glWidget->other_slices[index].source_images.geometry())
        {
            const float* buf = cur_tracking_window.glWidget->mi3s[index].get();
            convert.resize(16);
            image::create_affine_transformation_matrix(buf, buf + 9,convert.begin(), image::vdim<3>());
            break;
        }
    }

    if(from.geometry() != cur_tracking_window.slice.geometry &&
            !cur_tracking_window.handle->fib_data.trans_to_mni.empty() && convert.empty())// use transformation information
    {
        QMessageBox::information(this,"Warning","The nii file has different image dimension. Transformation will be applied to load the region",0);
        std::vector<float> t(header.get_transformation(),
                             header.get_transformation()+12),inv_trans(16);
        convert.resize(16);
        t.resize(16);
        t[15] = 1.0;
        image::matrix::inverse(t.begin(),inv_trans.begin(),image::dim<4,4>());
        image::matrix::product(inv_trans.begin(),
                               cur_tracking_window.handle->fib_data.trans_to_mni.begin(),
                               convert.begin(),image::dim<4,4>(),image::dim<4,4>());
    }
    else
    {
        // from +x = Right  +y = Anterior +z = Superior
        // to +x = Left  +y = Posterior +z = Superior
        if(header.nif_header.srow_x[0] < 0)
        {
            if(header.nif_header.srow_y[1] > 0)
                image::flip_y(from);
        }
        else
            image::flip_xy(from);
    }

    if(!multiple_roi)
    {
        ROIRegion region(cur_tracking_window.slice.geometry,cur_tracking_window.slice.voxel_size);

        if(convert.empty())
            region.LoadFromBuffer(from);
        else
            region.LoadFromBuffer(from,convert);
        max_value = *std::max_element(from.begin(),from.end());
        if(max_value > 128 && max_value < 0x00FFFFFF)
        {
            region.show_region.color = max_value;
            add_region(QFileInfo(file_name).baseName(),roi_id,0xFF000000 | max_value);
        }
        else
            add_region(QFileInfo(file_name).baseName(),roi_id);

        regions.back().assign(region.get());
        item(currentRow(),0)->setCheckState(Qt::Checked);
        item(currentRow(),0)->setData(Qt::ForegroundRole,QBrush(Qt::black));
        return true;
    }

    for(unsigned int value = 1;check_prog(value,value_map.size());++value)
        if(value_map[value])
        {
            image::basic_image<unsigned char,3> mask(from.geometry());
            for(unsigned int i = 0;i < mask.size();++i)
                if(from[i] == value)
                    mask[i] = 1;
            ROIRegion region(cur_tracking_window.slice.geometry,cur_tracking_window.slice.voxel_size);
            if(convert.empty())
                region.LoadFromBuffer(mask);
            else
                region.LoadFromBuffer(mask,convert);
            QString name = (label_map.find(value) == label_map.end() ? QString::number(value):label_map[value].c_str());
            add_region(QFileInfo(file_name).baseName()+"_"+name,roi_id);
            regions.back().assign(region.get());
            item(currentRow(),0)->setCheckState(Qt::Unchecked);
            item(currentRow(),0)->setData(Qt::ForegroundRole,QBrush(Qt::gray));
        }
    return true;
}


void RegionTableWidget::load_region(void)
{
    QStringList filenames = QFileDialog::getOpenFileNames(
                                this,
                                "Open region",
                                cur_tracking_window.get_path("region"),
                                "Region files (*.txt *.nii *.hdr *.nii.gz *.mat);;All files (*)" );
    if (filenames.isEmpty())
        return;
    cur_tracking_window.add_path("region",filenames[0]);

    for (unsigned int index = 0;index < filenames.size();++index)
    {
        // check for multiple nii
        if((QFileInfo(filenames[index]).suffix() == "gz" ||
           QFileInfo(filenames[index]).suffix() == "nii") &&
                load_multiple_roi_nii(filenames[index]))
            continue;

        ROIRegion region(cur_tracking_window.slice.geometry,cur_tracking_window.slice.voxel_size);
        if(!region.LoadFromFile(filenames[index].toLocal8Bit().begin(),
                cur_tracking_window.handle->fib_data.trans_to_mni))
        {
            QMessageBox::information(this,"error","Unknown file format",0);
            return;
        }
        add_region(QFileInfo(filenames[index]).baseName(),roi_id,
                   (region.show_region.color == image::rgb_color(0x00FFFFFF)) ? 0 : (int)(0xFF000000 | region.show_region.color.color));
        regions.back().assign(region.get());

    }
    emit need_update();
}

void RegionTableWidget::check_all(void)
{
    for(unsigned int row = 0;row < rowCount();++row)
    {
        item(row,0)->setCheckState(Qt::Checked);
        item(row,0)->setData(Qt::ForegroundRole,QBrush(Qt::black));
    }
    emit need_update();
}

void RegionTableWidget::uncheck_all(void)
{
    for(unsigned int row = 0;row < rowCount();++row)
    {
        item(row,0)->setCheckState(Qt::Unchecked);
        item(row,0)->setData(Qt::ForegroundRole,QBrush(Qt::gray));
    }
    emit need_update();
}

void RegionTableWidget::save_region(void)
{
    if (currentRow() >= regions.size())
        return;
    QSettings settings;
    QString filename = QFileDialog::getSaveFileName(
                           this,
                           "Save region",
                cur_tracking_window.get_path("region") + "/" + item(currentRow(),0)->text() + "." + settings.value("region_save_type","nii.gz").toString(),
                           "Region file(*.nii.gz *.nii *.txt *.mat);;All file types (*)" );
    if (filename.isEmpty())
        return;
    settings.setValue("region_save_type",QFileInfo(filename).completeSuffix());
    cur_tracking_window.add_path("region",filename);
    std::vector<float> no_trans;
    regions[currentRow()].SaveToFile(filename.toLocal8Bit().begin(),
                                     cur_tracking_window.is_qsdr ? cur_tracking_window.handle->fib_data.trans_to_mni: no_trans);
    item(currentRow(),0)->setText(QFileInfo(filename).baseName());
}
void RegionTableWidget::save_region_info(void)
{
    if (currentRow() >= regions.size())
        return;
    QString filename = QFileDialog::getSaveFileName(
                           this,
                           "Save voxel information",
                           cur_tracking_window.get_path("region") + "/" + item(currentRow(),0)->text() + "_info.txt",
                           "Text files (*.txt)" );
    if (filename.isEmpty())
        return;
    cur_tracking_window.add_path("region",filename);

    std::ofstream out(filename.toLocal8Bit().begin());
    out << "x\ty\tz";
    for(unsigned int index = 0;index < cur_tracking_window.handle->fib_data.fib.num_fiber;++index)
            out << "\tdx" << index << "\tdy" << index << "\tdz" << index;

    for(unsigned int index = 0;index < cur_tracking_window.handle->fib_data.view_item.size();++index)
        if(cur_tracking_window.handle->fib_data.view_item[index].name != "color")
            out << "\t" << cur_tracking_window.handle->fib_data.view_item[index].name;

    out << std::endl;
    for(int index = 0;index < regions[currentRow()].get().size();++index)
    {
        std::vector<float> data;
        image::vector<3,short> point(regions[currentRow()].get()[index]);
        cur_tracking_window.handle->get_voxel_info2(point[0],point[1],point[2],data);
        cur_tracking_window.handle->get_voxel_information(point[0],point[1],point[2],data);
        std::copy(point.begin(),point.end(),std::ostream_iterator<float>(out,"\t"));
        std::copy(data.begin(),data.end(),std::ostream_iterator<float>(out,"\t"));
        out << std::endl;
    }
}

void RegionTableWidget::delete_region(void)
{
    if (currentRow() >= regions.size())
        return;
    regions.erase(regions.begin()+currentRow());
    this->removeRow(currentRow());
    emit need_update();
}

void RegionTableWidget::delete_all_region(void)
{
    setRowCount(0);
    regions.clear();
    regions_index = 0;
    emit need_update();
}

void RegionTableWidget::whole_brain_points(std::vector<image::vector<3,short> >& points)
{
    float fa[3];
    float dir[9];
    image::geometry<3> geo = cur_tracking_window.slice.geometry;
    float threshold = cur_tracking_window.ui->fa_threshold->value();
    for (image::pixel_index<3>index; index.valid(geo);index.next(geo))
    {
        image::vector<3,short> pos(index);
        if(cur_tracking_window.handle->fib_data.fib.fa[0][index.index()] > threshold)
            points.push_back(pos);
    }
}

void RegionTableWidget::whole_brain(void)
{
    std::vector<image::vector<3,short> > points;
    whole_brain_points(points);
    add_region("whole brain",seed_id);
    add_points(points,false);
    emit need_update();
}

void RegionTableWidget::show_statistics(void)
{
    if(currentRow() >= regions.size())
        return;
    std::string result;
    {

        std::vector<std::string> titles;
        titles.push_back("voxel counts");
        titles.push_back("volume (mm^3)");
        titles.push_back("center x");
        titles.push_back("center y");
        titles.push_back("center z");
        titles.push_back("bounding box x");
        titles.push_back("bounding box y");
        titles.push_back("bounding box z");
        titles.push_back("bounding box x");
        titles.push_back("bounding box y");
        titles.push_back("bounding box z");
        cur_tracking_window.handle->get_index_titles(titles);

        std::vector<std::vector<float> > data(regions.size());
        begin_prog("calculating");
        for(unsigned int index = 0;check_prog(index,regions.size());++index)
            regions[index].get_quantitative_data(cur_tracking_window.handle,data[index]);
        if(prog_aborted())
            return;
        std::ostringstream out;
        out << "Name\t";
        for(unsigned int index = 0;index < regions.size();++index)
            out << item(index,0)->text().toLocal8Bit().begin() << "\t";
        out << std::endl;
        for(unsigned int i = 0;i < titles.size();++i)
        {
            out << titles[i] << "\t";
            for(unsigned int j = 0;j < regions.size();++j)
            {
                if(i < data[j].size())
                    out << data[j][i];
                out << "\t";
            }
            out << std::endl;
        }
        result = out.str();
    }
    QMessageBox msgBox;
    msgBox.setText("Region Statistics");
    msgBox.setInformativeText(result.c_str());
    msgBox.setStandardButtons(QMessageBox::Ok|QMessageBox::Save);
    msgBox.setDefaultButton(QMessageBox::Ok);
    QPushButton *copyButton = msgBox.addButton("Copy To Clipboard", QMessageBox::ActionRole);


    if(msgBox.exec() == QMessageBox::Save)
    {
        QString filename;
        filename = QFileDialog::getSaveFileName(
                    this,
                    "Save satistics as",
                    cur_tracking_window.get_path("region") + +"/" + item(currentRow(),0)->text() + "_stat.txt",
                    "Text files (*.txt);;All files|(*)");
        if(filename.isEmpty())
            return;
        cur_tracking_window.add_path("region",filename);
        std::ofstream out(filename.toLocal8Bit().begin());
        out << result.c_str();
    }
    if (msgBox.clickedButton() == copyButton)
        QApplication::clipboard()->setText(result.c_str());
}

extern std::vector<float> mni_fa0_template_tran;
extern image::basic_image<float,3> mni_fa0_template;

void RegionTableWidget::add_atlas(void)
{
    if(cur_tracking_window.handle->fib_data.trans_to_mni.empty())
        return;
    int atlas_index = cur_tracking_window.ui->atlasListBox->currentIndex();
    std::vector<image::vector<3,short> > points;
    unsigned short label = cur_tracking_window.ui->atlasComboBox->currentIndex();
    image::geometry<3> geo = cur_tracking_window.slice.geometry;
    for (image::pixel_index<3>index; index.valid(geo);index.next(geo))
    {
        image::vector<3,float> mni((const unsigned int*)(index.begin()));
        cur_tracking_window.subject2mni(mni);
        if (!atlas_list[atlas_index].is_labeled_as(mni, label))
                continue;
        points.push_back(image::vector<3,short>((const unsigned int*)index.begin()));
    }
    add_region(cur_tracking_window.ui->atlasComboBox->currentText(),roi_id);
    add_points(points,false);
    emit need_update();
}

void RegionTableWidget::add_points(std::vector<image::vector<3,short> >& points,bool erase)
{
    if (currentRow() >= regions.size())
        return;
    regions[currentRow()].add_points(points,erase);
    item(currentRow(),0)->setCheckState(Qt::Checked);
    item(currentRow(),0)->setData(Qt::ForegroundRole,QBrush(Qt::black));
}

bool RegionTableWidget::has_seeding(void)
{
    for (unsigned int index = 0;index < regions.size();++index)
        if (!regions[index].empty() &&
                item(index,0)->checkState() == Qt::Checked &&
                regions[index].regions_feature == seed_id) // either roi roa end or seed
            return true;
    return false;
}

void RegionTableWidget::setROIs(ThreadData* data)
{
    // check if there is seeds
    if(!has_seeding())
    {
        std::vector<image::vector<3,short> > points;
        whole_brain_points(points);
        data->setRegions(cur_tracking_window.handle->fib_data.dim,points,seed_id);
    }

    for (unsigned int index = 0;index < regions.size();++index)
        if (!regions[index].empty() &&
                item(index,0)->checkState() == Qt::Checked &&
                regions[index].regions_feature <= 3) // either roi roa end or seed
            data->setRegions(cur_tracking_window.handle->fib_data.dim,regions[index].get(),
                             regions[index].regions_feature);
}


void RegionTableWidget::do_action(int id)
{
    if (regions.empty())
        return;
    image::basic_image<unsigned char, 3>mask;
    ROIRegion& cur_region = regions[currentRow()];
    switch (id)
    {
    case 0: // Smoothing
        cur_region.SaveToBuffer(mask, 1);
        image::morphology::smoothing(mask);
        cur_region.LoadFromBuffer(mask);
        break;
    case 1: // Erosion
        cur_region.SaveToBuffer(mask, 1);
        image::morphology::erosion(mask);
        cur_region.LoadFromBuffer(mask);
        break;
    case 2: // Expansion
        cur_region.SaveToBuffer(mask, 1);
        image::morphology::dilation(mask);
        cur_region.LoadFromBuffer(mask);
        break;
    case 3: // Defragment
        cur_region.SaveToBuffer(mask, 1);
        image::morphology::defragment(mask);
        cur_region.LoadFromBuffer(mask);
        break;
    case 4: // Negate
        cur_region.SaveToBuffer(mask, 1);
        for (unsigned int index = 0; index < mask.size(); ++index)
            mask[index] = 1 - mask[index];
        cur_region.LoadFromBuffer(mask);
        break;
    case 5:
        cur_region.Flip(0);
        break;
    case 6:
        cur_region.Flip(1);
        break;
    case 7:
        cur_region.Flip(2);
        break;
    case 8: //
        {
            float threshold = cur_tracking_window.ui->fa_threshold->value();
            for(int index = 0;index < cur_region.size();)
            {
                image::vector<3,short> point(cur_region.get()[index]);
                if(threshold > ((const FibSliceModel&)cur_tracking_window.slice).source_images.at(point[0],point[1],point[2]))
                    cur_region.erase(index);
                else
                    ++index;
            }
        }
        break;
    case 9: // shift
        cur_region.shift(image::vector<3,short>(1, 0, 0));
        break;
    case 10: // shift
        cur_region.shift(image::vector<3,short>(-1, 0, 0));
        break;
    case 11: // shift
        cur_region.shift(image::vector<3,short>(0, 1, 0));
        break;
    case 12: // shift
        cur_region.shift(image::vector<3,short>(0, -1, 0));
        break;
    case 13: // shift
        cur_region.shift(image::vector<3,short>(0, 0, 1));
        break;
    case 14: // shift
        cur_region.shift(image::vector<3,short>(0, 0, -1));
        break;
    }
    emit need_update();
}
