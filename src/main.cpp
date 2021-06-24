#include "main.h"

using std::shared_ptr;
using std::unique_ptr;
using std::vector;

int main(int argc, char *argv[])
{
    assert(argc == 3); // path to statistics and path to sstable

    // TODO check files exist

    std::shared_ptr<sstable_statistics_t> statistics;
    read_statistics(argv[1], &statistics);
    std::shared_ptr<sstable_data_t> sstable;
    read_data(argv[2], &sstable);

    std::shared_ptr<arrow::Table> table;
    std::shared_ptr<arrow::Schema> schema;
    EXIT_ON_FAILURE(vector_to_columnar_table(sstable, &schema, &table));
    send_data(schema, table);

    return 0;
}

void read_index(std::string path, std::shared_ptr<sstable_index_t> *index)
{
    std::ifstream ifs(path, std::ifstream::binary);
    kaitai::kstream ks(&ifs);
    *index = std::make_shared<sstable_index_t>(&ks);

    for (unique_ptr<sstable_index_t::index_entry_t> &entry : *index->get()->entries())
    {
        std::cout
            << "key: " << entry->key() << "\n"
            << "position: " << entry->position()->val() << "\n";

        if (entry->promoted_index_length()->val() > 0)
        {
            std::cout << "promoted index exists\n";
        }
    }
}

void read_statistics(std::string path, std::shared_ptr<sstable_statistics_t> *statistics)
{
    std::cout << "\n\n===== READING STATISTICS =====\n";

    std::ifstream ifs(path, std::ifstream::binary);
    kaitai::kstream ks(&ifs);
    *statistics = std::make_shared<sstable_statistics_t>(&ks);

    auto &ptr = (*statistics->get()->toc()->array())[3];
    sstable_statistics_t::serialization_header_t *body = (sstable_statistics_t::serialization_header_t *)ptr->body();

    std::cout << "\npartition key type: " << body->partition_key_type()->body() << "\n";

    std::cout
        << "min ttl: " << body->min_ttl()->val() << '\n'
        << "min timestamp: " << body->min_timestamp()->val() << '\n'
        << "min local deletion time: " << body->min_local_deletion_time()->val() << '\n';

    int i;

    // Set important constants for the serialization helper and initialize vectors to store
    // types of clustering columns
    deserialization_helper_t::set_n_cols(deserialization_helper_t::CLUSTERING, body->clustering_key_types()->length()->val());
    deserialization_helper_t::set_n_cols(deserialization_helper_t::STATIC, body->static_columns()->length()->val());
    deserialization_helper_t::set_n_cols(deserialization_helper_t::REGULAR, body->regular_columns()->length()->val());

    std::cout << "\n=== clustering keys (" << body->clustering_key_types()->length()->val() << ") ===\n";
    i = 0;
    for (auto &type : *body->clustering_key_types()->array())
    {
        std::cout << "type: " << type->body() << "\n";
        deserialization_helper_t::set_col_type(deserialization_helper_t::CLUSTERING, i++, type->body());
    }

    std::cout << "\n=== static columns (" << body->static_columns()->length()->val() << ") ===\n";
    i = 0;
    for (auto &column : *body->static_columns()->array())
    {
        std::cout
            << "name: " << column->name()->body() << "\n"
            << "type: " << column->column_type()->body() << '\n';
        deserialization_helper_t::set_col_type(deserialization_helper_t::STATIC, i++, column->name()->body());
    }

    std::cout << "\n=== regular columns (" << body->regular_columns()->length()->val() << ") ===\n";
    i = 0;
    for (auto &column : *body->regular_columns()->array())
    {
        std::cout
            << "name: " << column->name()->body() << "\n"
            << "type: " << column->column_type()->body() << '\n';
        deserialization_helper_t::set_col_type(deserialization_helper_t::REGULAR, i++, column->name()->body());
    }
}

void read_data(std::string path, std::shared_ptr<sstable_data_t> *sstable)
{
    std::cout << "\n\n===== READING DATA =====\n";

    std::ifstream ifs(path, std::ifstream::binary);
    kaitai::kstream ks(&ifs);
    *sstable = std::make_shared<sstable_data_t>(&ks);

    for (auto &partition : *sstable->get()->partitions())
    {
        std::cout << "\n========== partition ==========\nkey: " << partition->header()->key() << '\n';

        for (auto &unfiltered : *partition->unfiltereds())
        {
            if ((unfiltered->flags() & 0x01) != 0)
                break;

            // u->body()->_io()
            if ((unfiltered->flags() & 0x02) != 0) // range tombstone marker
            {
                std::cout << "range tombstone marker\n";
                sstable_data_t::range_tombstone_marker_t *marker = (sstable_data_t::range_tombstone_marker_t *)unfiltered->body();
            }
            else
            {
                std::cout << "\n=== row ===\n";
                sstable_data_t::row_t *row = (sstable_data_t::row_t *)unfiltered->body();
                for (auto &cell : *row->clustering_blocks()->values())
                {
                    std::cout << "clustering cell: " << cell << '\n';
                }

                for (auto &cell : *row->cells())
                {
                    sstable_data_t::simple_cell_t *simple_cell = (sstable_data_t::simple_cell_t *)cell.get();
                    std::cout << "cell value: " << simple_cell->value()->value() << "\n";
                }
            }
        }
    }
}
