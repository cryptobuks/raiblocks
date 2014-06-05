#include <mu_coin/mu_coin.hpp>

#include <cryptopp/sha.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <ed25519-donna/ed25519.h>

mu_coin::uint256_union::uint256_union (boost::multiprecision::uint256_t const & number_a)
{
    boost::multiprecision::uint256_t number_l (number_a);
    qwords [0] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [1] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [2] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [3] = number_l.convert_to <uint64_t> ();
    std::reverse (&bytes [0], &bytes [32]);
}

mu_coin::uint512_union::uint512_union (boost::multiprecision::uint512_t const & number_a)
{
    boost::multiprecision::uint512_t number_l (number_a);
    qwords [0] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [1] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [2] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [3] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [4] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [5] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [6] = number_l.convert_to <uint64_t> ();
    number_l >>= 64;
    qwords [7] = number_l.convert_to <uint64_t> ();
    std::reverse (&bytes [0], &bytes [64]);
}

void mu_coin::uint256_union::clear ()
{
    bytes.fill (0);
}

void mu_coin::uint512_union::clear ()
{
    bytes.fill (0);
}

void hash_number (CryptoPP::SHA256 & hash_a, boost::multiprecision::uint256_t const & number_a)
{
    mu_coin::uint256_union bytes (number_a);
    hash_a.Update (bytes.bytes.data (), sizeof (bytes));
}

mu_coin::uint256_t mu_coin::uint256_union::number () const
{
    mu_coin::uint256_union temp (*this);
    std::reverse (&temp.bytes [0], &temp.bytes [32]);
    boost::multiprecision::uint256_t result (temp.qwords [3]);
    result <<= 64;
    result |= temp.qwords [2];
    result <<= 64;
    result |= temp.qwords [1];
    result <<= 64;
    result |= temp.qwords [0];
    return result;
}

boost::multiprecision::uint512_t mu_coin::uint512_union::number ()
{
    mu_coin::uint512_union temp (*this);
    std::reverse (&temp.bytes [0], &temp.bytes [64]);
    boost::multiprecision::uint512_t result (temp.qwords [7]);
    result <<= 64;
    result |= temp.qwords [6];
    result <<= 64;
    result |= temp.qwords [5];
    result <<= 64;
    result |= temp.qwords [4];
    result <<= 64;
    result |= temp.qwords [3];
    result <<= 64;
    result |= temp.qwords [2];
    result <<= 64;
    result |= temp.qwords [1];
    result <<= 64;
    result |= temp.qwords [0];
    return result;
}

void mu_coin::sign_message (mu_coin::private_key const & private_key, mu_coin::public_key const & public_key, mu_coin::uint256_union const & message, mu_coin::uint512_union & signature)
{
    ed25519_sign (message.bytes.data (), sizeof (message.bytes), private_key.bytes.data (), public_key.bytes.data (), signature.bytes.data ());
}

bool mu_coin::validate_message (mu_coin::public_key const & public_key, mu_coin::uint256_union const & message, mu_coin::uint512_union const & signature)
{
    auto result (0 != ed25519_sign_open (message.bytes.data (), sizeof (message.bytes), public_key.bytes.data (), signature.bytes.data ()));
    return result;
}

class balance_visitor : public mu_coin::block_visitor
{
public:
    balance_visitor (mu_coin::ledger & ledger_a, mu_coin::address const & address_a, mu_coin::block_hash & hash_a, bool & done_a, mu_coin::uint256_t & result_a) :
    ledger (ledger_a),
    address (address_a),
    hash (hash_a),
    done (done_a),
    result (result_a)
    {
    }
    void send_block (mu_coin::send_block const & block_a) override
    {
        auto entry (std::find_if (block_a.inputs.begin (), block_a.inputs.end (),
            [this] (mu_coin::send_input const & input_a)
            {
                mu_coin::block_hash hash;
                auto error (ledger.store.identifier_get (input_a.previous, hash));
                assert (!error);
                return (hash ^ input_a.previous) == address;
            }
        ));
        assert (entry != block_a.inputs.end ());
        result = entry->coins.number ();
        done = true;
    }
    void receive_block (mu_coin::receive_block const & block_a) override
    {
        auto source (ledger.store.block_get (block_a.source));
        assert (source != nullptr);
        assert (dynamic_cast <mu_coin::send_block *> (source.get ()) != nullptr);
        auto send (static_cast <mu_coin::send_block *> (source.get ()));
        auto entry (std::find_if (send->outputs.begin (), send->outputs.end (), [this] (mu_coin::send_output const & output_a) {return output_a.destination == address;}));
        assert (entry != send->outputs.end ());
        result += entry->coins.number ();
        hash = block_a.previous;
    }
    mu_coin::ledger & ledger;
    mu_coin::address address;
    mu_coin::block_hash & hash;
    bool & done;
    mu_coin::uint256_t & result;
};

mu_coin::uint256_t mu_coin::ledger::balance (mu_coin::address const & address_a)
{
    mu_coin::uint256_t result (0);
    mu_coin::block_hash hash;
    auto done (store.latest_get (address_a, hash));
    while (!done)
    {
        if (address_a == hash)
        {
            // Initial previous
            done = true;
        }
        else
        {
            auto block (store.block_get (hash));
            assert (block != nullptr);
            balance_visitor visitor (*this, address_a, hash, done, result);
            block->visit (visitor);
        }
    }
    return result;
}

bool mu_coin::ledger::process (mu_coin::block const & block_a)
{
    mu_coin::ledger_processor processor (*this);
    block_a.visit (processor);
    return processor.result;
}

mu_coin::keypair::keypair ()
{
    ed25519_randombytes_unsafe (prv.bytes.data (), sizeof (prv.bytes));
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

mu_coin::ledger::ledger (mu_coin::block_store & store_a) :
store (store_a)
{
}

bool mu_coin::uint256_union::operator == (mu_coin::uint256_union const & other_a) const
{
    return bytes == other_a.bytes;
}

bool mu_coin::uint512_union::operator == (mu_coin::uint512_union const & other_a) const
{
    return bytes == other_a.bytes;
}

size_t mu_coin::byte_read_stream::byte_read_stream::size ()
{
    return end - data;
}

mu_coin::byte_read_stream::byte_read_stream (uint8_t const * data_a, uint8_t const * end_a) :
data (data_a),
end (end_a)
{
}

mu_coin::byte_read_stream::byte_read_stream (uint8_t const * data_a, size_t const size_a) :
data (data_a),
end (data_a + size_a)
{
}

bool mu_coin::byte_read_stream::read (uint8_t * value, size_t size_a)
{
    auto result (false);
    if (data + size_a <= end)
    {
        std::copy (data, data + size_a, value);
        data += size_a;
    }
    else
    {
       result = true;
    }
    return result;
}

mu_coin::byte_write_stream::byte_write_stream () :
data (nullptr),
size (0)
{
}

mu_coin::byte_write_stream::~byte_write_stream ()
{
    free (data);
}

void mu_coin::byte_write_stream::extend (size_t additional)
{
    data = size ? reinterpret_cast <uint8_t *> (realloc (data, size + additional)) : reinterpret_cast <uint8_t *> (malloc (additional));
    size += additional;
}

void mu_coin::byte_write_stream::write (uint8_t const * data_a, size_t size_a)
{
    extend (size_a);
    std::copy (data_a, data_a + size_a, data + size - size_a);
}

void mu_coin::uint256_union::serialize (mu_coin::byte_write_stream & stream_a) const
{
    stream_a.write (bytes);
}

bool mu_coin::uint256_union::deserialize (mu_coin::byte_read_stream & stream_a)
{
    auto & point_l (bytes);
    return stream_a.read (point_l);
}

mu_coin::uint256_union::uint256_union (mu_coin::private_key const & prv, uint256_union const & key, uint128_union const & iv)
{
    mu_coin::uint256_union exponent (prv);
    CryptoPP::AES::Encryption alg (key.bytes.data (), sizeof (key.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
    enc.ProcessData (bytes.data (), exponent.bytes.data (), sizeof (exponent.bytes));
}

mu_coin::private_key mu_coin::uint256_union::prv (mu_coin::secret_key const & key_a, uint128_union const & iv) const
{
    CryptoPP::AES::Decryption alg (key_a.bytes.data (), sizeof (key_a.bytes));
    CryptoPP::CBC_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
    mu_coin::private_key result;
    dec.ProcessData (result.bytes.data (), bytes.data (), sizeof (bytes));
    return result;
}

mu_coin::uint256_union::uint256_union (std::string const & password_a)
{
    CryptoPP::SHA256 hash;
    hash.Update (reinterpret_cast <uint8_t const *> (password_a.c_str ()), password_a.size ());
    hash.Final (bytes.data ());
}

void mu_coin::send_block::visit (mu_coin::block_visitor & visitor_a) const
{
    visitor_a.send_block (*this);
}

void mu_coin::receive_block::visit (mu_coin::block_visitor & visitor_a) const
{
    visitor_a.receive_block (*this);
}

mu_coin::uint256_t mu_coin::send_block::fee () const
{
    return 1;
}

mu_coin::uint256_union mu_coin::send_block::hash () const
{
    mu_coin::uint256_union result;
    CryptoPP::SHA256 hash;
    for (auto & i: inputs)
    {
        hash.Update (i.previous.bytes.data (), sizeof (i.previous.bytes));
        hash.Update (i.coins.bytes.data (), sizeof (i.coins.bytes));
    }
    for (auto & i: outputs)
    {
        hash.Update (i.destination.bytes.data (), sizeof (i.destination.bytes));
        hash.Update (i.coins.bytes.data (), sizeof (i.coins.bytes));
    }
    hash.Final (result.bytes.data ());
    return result;
}

void mu_coin::send_block::serialize (mu_coin::byte_write_stream & stream) const
{
    uint8_t input_count (inputs.size ());
    stream.write (input_count);
    uint8_t output_count (outputs.size ());
    stream.write (output_count);
    for (auto & i: inputs)
    {
        stream.write (i.previous.bytes);
        stream.write (i.coins.bytes);
    }
    for (auto & i: outputs)
    {
        stream.write (i.destination.bytes);
        stream.write (i.coins.bytes);
    }
    for (auto & i: signatures)
    {
        stream.write (i.bytes);
    }
}

bool mu_coin::send_block::deserialize (mu_coin::byte_read_stream & stream)
{
    auto result (false);
    uint8_t input_count;
    result = stream.read (input_count);
    if (!result)
    {
        uint8_t output_count;
        result = stream.read (output_count);
        if (!result)
        {
            inputs.reserve (input_count);
            outputs.reserve (output_count);
            for (uint8_t i (0); !result && i < input_count; ++i)
            {
                inputs.push_back (mu_coin::send_input ());
                auto & back (inputs.back ());
                result = stream.read (back.previous.bytes);
                if (!result)
                {
                    result = stream.read (back.coins.bytes);
                }
            }
            for (uint8_t i (0); !result && i < output_count; ++i)
            {
                outputs.push_back (mu_coin::send_output ());
                auto & back (outputs.back ());
                result = stream.read (back.destination.bytes);
                if (!result)
                {
                    result = stream.read (back.coins);
                }
            }
            for (uint8_t i (0); !result && i < input_count; ++i)
            {
                signatures.push_back (mu_coin::uint512_union ());
                result = stream.read (signatures.back ().bytes);
            }
        }
    }
    return result;
}

bool mu_coin::send_block::operator == (mu_coin::send_block const & other_a) const
{
    auto result (inputs == other_a.inputs && outputs == other_a.outputs && signatures == other_a.signatures);
    return result;
}

bool mu_coin::send_input::send_input::operator == (mu_coin::send_input const & other_a) const
{
    auto result (previous == other_a.previous && coins == other_a.coins);
    return result;
}

mu_coin::send_input::send_input (mu_coin::public_key const & pub_a, mu_coin::block_hash const & hash_a, mu_coin::balance const & coins_a) :
previous (pub_a ^ hash_a),
coins (coins_a)
{
}

mu_coin::send_output::send_output (mu_coin::public_key const & pub, mu_coin::uint256_union const & coins_a) :
destination (pub),
coins (coins_a)
{
}

bool mu_coin::send_output::operator == (mu_coin::send_output const & other_a) const
{
    auto result (destination == other_a.destination && coins == other_a.coins);
    return result;
}

void mu_coin::receive_block::sign (mu_coin::private_key const & prv, mu_coin::public_key const & pub, mu_coin::uint256_union const & message)
{
    sign_message (prv, pub, message, signature);
}

bool mu_coin::receive_block::operator == (mu_coin::receive_block const & other_a) const
{
    auto result (signature == other_a.signature && source == other_a.source && previous == other_a.previous);
    return result;
}

bool mu_coin::receive_block::deserialize (mu_coin::byte_read_stream & stream_a)
{
    auto result (false);
    result = stream_a.read (signature.bytes);
    if (!result)
    {
        result = stream_a.read (source.bytes);
        if (!result)
        {
            result = stream_a.read (previous.bytes);
        }
    }
    return result;
}

void mu_coin::receive_block::serialize (mu_coin::byte_write_stream & stream_a) const
{
    stream_a.write (signature.bytes);
    stream_a.write (source.bytes);
    stream_a.write (previous.bytes);
}

mu_coin::uint256_t mu_coin::receive_block::fee () const
{
    return 1;
}

mu_coin::uint256_union mu_coin::receive_block::hash () const
{
    CryptoPP::SHA256 hash;
    hash.Update (source.bytes.data (), sizeof (source.bytes));
    hash.Update (previous.bytes.data (), sizeof (previous.bytes));
    mu_coin::uint256_union result;
    hash.Final (result.bytes.data ());
    return result;
}

void mu_coin::ledger_processor::send_block (mu_coin::send_block const & block_a)
{
    mu_coin::uint256_union message (block_a.hash ());
    mu_coin::uint256_t inputs (0);
    std::vector <mu_coin::address> input_addresses;
    result = block_a.inputs.size () != block_a.signatures.size ();
    if (!result)
    {
        auto k (block_a.signatures.begin ());
        for (auto i (block_a.inputs.begin ()), j (block_a.inputs.end ()); !result && i != j; ++i, ++k)
        {
            mu_coin::uint256_union block_hash;
            result = ledger.store.identifier_get (i->previous, block_hash);
            if (!result)
            {
                auto address (block_hash ^ i->previous);
                result = validate_message (address, message, *k);
                if (!result)
                {
                    input_addresses.push_back (address);
                    auto existing (ledger.store.block_get (block_hash));
                    assert (existing != nullptr);
                    mu_coin::uint256_union coins (ledger.balance (address));
                    auto coins_string (coins.number ().convert_to <std::string> ());
                    auto diff (coins.number () - i->coins.number ());
                    inputs += diff;
                    result = diff > coins.number ();
                }
            }
        }
    }
    mu_coin::uint256_t outputs (0);
    for (auto i (block_a.outputs.begin ()), j (block_a.outputs.end ()); i != j && !result; ++i)
    {
        outputs += i->coins.number ();
    }
    auto inputs_string (inputs.convert_to<std::string>());
    auto outputs_string (outputs.convert_to<std::string>());
    if (!result && outputs + block_a.fee () == inputs)
    {
        ledger.store.block_put (message, block_a);
        auto k (input_addresses.begin ());
        for (auto i (block_a.inputs.begin ()), j (block_a.inputs.end ()); i != j; ++i, ++k)
        {
            assert (k != input_addresses.end ());
            ledger.store.identifier_put (message ^ *k, message);
            ledger.store.latest_put (*k, message);
        }
        for (auto i (block_a.outputs.begin ()), j (block_a.outputs.end ()); i != j; ++i)
        {
            ledger.store.pending_put (i->destination, message);
        }
    }
    else
    {
        result = true;
    }
}

void mu_coin::ledger_processor::receive_block (mu_coin::receive_block const & block_a)
{
    mu_coin::block_hash previous_hash;
    auto new_address (ledger.store.identifier_get (block_a.previous, previous_hash));
    mu_coin::address address;
    if (new_address)
    {
        address = block_a.previous;
        mu_coin::block_hash latest;
        result = !ledger.store.latest_get (address, latest);
    }
    else
    {
        address = previous_hash ^ block_a.previous;
        mu_coin::block_hash latest;
        result = ledger.store.latest_get (address, latest);
        if (!result)
        {
            result = (latest == previous_hash);
        }
    }
    if (!result)
    {
        auto hash (block_a.hash ());
        result = ledger.store.pending_get (address, block_a.source);
        if (!result)
        {
            ledger.store.pending_del (address, block_a.source);
            ledger.store.block_put (hash, block_a);
            auto new_identifier (address ^ hash);
            ledger.store.identifier_put (new_identifier, hash);
            ledger.store.latest_put (address, hash);
        }
    }
}

mu_coin::ledger_processor::ledger_processor (mu_coin::ledger & ledger_a) :
ledger (ledger_a)
{
}

mu_coin::send_block::send_block (send_block const & other_a) :
inputs (other_a.inputs),
outputs (other_a.outputs),
signatures (other_a.signatures)
{
}

bool mu_coin::receive_block::validate (mu_coin::public_key const & key, mu_coin::uint256_t const & hash) const
{
    return validate_message (key, hash, signature);
}

bool mu_coin::send_block::operator == (mu_coin::block const & other_a) const
{
    auto other_l (dynamic_cast <mu_coin::send_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

bool mu_coin::receive_block::operator == (mu_coin::block const & other_a) const
{
    auto other_l (dynamic_cast <mu_coin::receive_block const *> (&other_a));
    auto result (other_l != nullptr);
    if (result)
    {
        result = *this == *other_l;
    }
    return result;
}

std::unique_ptr <mu_coin::block> mu_coin::send_block::clone () const
{
    return std::unique_ptr <mu_coin::block> (new mu_coin::send_block (*this));
}

std::unique_ptr <mu_coin::block> mu_coin::receive_block::clone () const
{
    return std::unique_ptr <mu_coin::block> (new mu_coin::receive_block (*this));
}

std::unique_ptr <mu_coin::block> mu_coin::deserialize_block (mu_coin::byte_read_stream & stream_a)
{
    mu_coin::block_type type;
    stream_a.read (type);
    std::unique_ptr <mu_coin::block> result;
    switch (type)
    {
        case mu_coin::block_type::receive:
        {
            std::unique_ptr <mu_coin::receive_block> obj (new mu_coin::receive_block);
            auto error (obj->deserialize (stream_a));
            if (!error)
            {
                result = std::move (obj);
            }
        }
            break;
        case mu_coin::block_type::send:
        {
            std::unique_ptr <mu_coin::send_block> obj (new mu_coin::send_block);
            auto error (obj->deserialize (stream_a));
            if (!error)
            {
                result = std::move (obj);
            }
        }
            break;
        default:
            assert (false);
            break;
    }
    return result;
}

void mu_coin::serialize_block (mu_coin::byte_write_stream & stream_a, mu_coin::block const & block_a)
{
    stream_a.write (block_a.type ());
    block_a.serialize (stream_a);
}

mu_coin::block_type mu_coin::send_block::type () const
{
    return mu_coin::block_type::send;
}

mu_coin::block_type mu_coin::receive_block::type () const
{
    return mu_coin::block_type::receive;
}

void mu_coin::cached_password_store::decrypt (mu_coin::uint256_union const & pin_hash, mu_coin::uint256_union & password_a)
{
    CryptoPP::AES::Decryption alg (pin_hash.bytes.data (), sizeof (pin_hash.bytes));
    CryptoPP::ECB_Mode_ExternalCipher::Decryption dec (alg);
    dec.ProcessData (password_a.bytes.data (), password.bytes.data (), sizeof (password.bytes));
}

void mu_coin::cached_password_store::encrypt (mu_coin::uint256_union const & pin_hash, mu_coin::uint256_union const & password_a)
{
    CryptoPP::AES::Encryption alg (pin_hash.bytes.data (), sizeof (pin_hash.bytes));
    CryptoPP::ECB_Mode_ExternalCipher::Encryption enc (alg);
    enc.ProcessData (password.bytes.data (), password_a.bytes.data (), sizeof (password_a.bytes));
}

mu_coin::cached_password_store::~cached_password_store ()
{
    clear ();
}

void mu_coin::cached_password_store::clear ()
{
    password.bytes.fill (0);
}

void mu_coin::uint256_union::encode_hex (std::string & text)
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (64) << std::setfill ('0');
    stream << number ();
    text = stream.str ();
}

bool mu_coin::uint256_union::decode_hex (std::string const & text)
{
    auto result (text.size () > 64);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::hex << std::noshowbase;
        mu_coin::uint256_t number_l;
        try
        {
            stream >> number_l;
            *this = number_l;
        }
        catch (std::runtime_error &)
        {
            result = true;
        }
    }
    return result;
}

void mu_coin::uint256_union::encode_dec (std::string & text)
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::dec << std::noshowbase;
    stream << number ();
    text = stream.str ();
}

bool mu_coin::uint256_union::decode_dec (std::string const & text)
{
    auto result (text.size () > 64);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::dec << std::noshowbase;
        mu_coin::uint256_t number_l;
        try
        {
            stream >> number_l;
            *this = number_l;
        }
        catch (std::runtime_error &)
        {
            result = true;
        }
    }
    return result;
}

void mu_coin::uint512_union::encode_hex (std::string & text)
{
    assert (text.empty ());
    std::stringstream stream;
    stream << std::hex << std::noshowbase << std::setw (128) << std::setfill ('0');
    stream << number ();
    text = stream.str ();
}

bool mu_coin::uint512_union::decode_hex (std::string const & text)
{
    auto result (text.size () > 128);
    if (!result)
    {
        std::stringstream stream (text);
        stream << std::hex << std::noshowbase;
        mu_coin::uint512_t number_l;
        try
        {
            stream >> number_l;
            *this = number_l;
        }
        catch (std::runtime_error &)
        {
            result = true;
        }
    }
    return result;
}

mu_coin::block_store_temp_t mu_coin::block_store_temp;

mu_coin::block_store::block_store (block_store_temp_t const &) :
block_store (boost::filesystem::unique_path ())
{
}

mu_coin::block_store::block_store (boost::filesystem::path const & path_a) :
handle (nullptr, 0)
{
    handle.open (nullptr, path_a.native().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
}

void mu_coin::block_store::block_put (mu_coin::block_hash const & hash_a, mu_coin::block const & block_a)
{
    dbt key (hash_a);
    dbt data (block_a);
    int error (handle.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

std::unique_ptr <mu_coin::block> mu_coin::block_store::block_get (mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (hash_a);
    mu_coin::dbt data;
    int error (handle.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    auto result (data.block ());
    return result;
}

void mu_coin::block_store::genesis_put (mu_coin::public_key const & key_a, uint256_union const & coins_a)
{
    mu_coin::send_block send;
    send.outputs.push_back (mu_coin::send_output (key_a, coins_a));
    auto hash1 (send.hash ());
    block_put (hash1, send);
    mu_coin::receive_block receive;
    receive.previous = key_a;
    receive.source = hash1;
    auto hash2 (receive.hash ());
    block_put (hash2, receive);
    identifier_put (key_a ^ hash2, hash2);
    latest_put (key_a, hash2);
}

bool mu_coin::block_store::latest_get (mu_coin::address const & address_a, mu_coin::block_hash & hash_a)
{
    mu_coin::dbt key (address_a);
    mu_coin::dbt data;
    int error (handle.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    bool result;
    if (data.data.get_size () == 0)
    {
        result = true;
    }
    else
    {
        mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t const *> (data.data.get_data ()), data.data.get_size ());
        stream.read (hash_a.bytes);
        result = false;
    }
    return result;
}

void mu_coin::block_store::latest_put (mu_coin::address const & address_a, mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (address_a);
    mu_coin::dbt data (hash_a);
    int error (handle.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

void mu_coin::block_store::pending_put (mu_coin::address const & address_a, mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (address_a, hash_a);
    mu_coin::dbt data;
    int error (handle.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

void mu_coin::block_store::pending_del (mu_coin::address const & address_a, mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (address_a, hash_a);
    mu_coin::dbt data;
    int error (handle.del (nullptr, &key.data, 0));
    assert (error == 0);
}

bool mu_coin::block_store::pending_get (mu_coin::address const & address_a, mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (address_a, hash_a);
    mu_coin::dbt data;
    int error (handle.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    bool result;
    if (error == DB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        result = false;
    }
    return result;
}

void mu_coin::block_store::identifier_put (mu_coin::identifier const & identifier_a, mu_coin::block_hash const & hash_a)
{
    mu_coin::dbt key (identifier_a);
    mu_coin::dbt data (hash_a);
    int error (handle.put (nullptr, &key.data, &data.data, 0));
    assert (error == 0);
}

bool mu_coin::block_store::identifier_get (mu_coin::identifier const & identifier_a, mu_coin::block_hash & hash_a)
{
    mu_coin::dbt key (identifier_a);
    mu_coin::dbt data;
    int error (handle.get (nullptr, &key.data, &data.data, 0));
    assert (error == 0 || error == DB_NOTFOUND);
    bool result;
    if (error == DB_NOTFOUND)
    {
        result = true;
    }
    else
    {
        hash_a = data.uint256 ();
        result = false;
    }
    return result;
}

std::unique_ptr <mu_coin::block> mu_coin::dbt::block()
{
    std::unique_ptr <mu_coin::block> result;
    if (data.get_size () > 0)
    {
        mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t *> (data.get_data ()), reinterpret_cast <uint8_t *> (data.get_data ()) + data.get_size ());
        result = mu_coin::deserialize_block (stream);
    }
    return result;
}

mu_coin::dbt::dbt (mu_coin::block const & block_a)
{
    mu_coin::byte_write_stream stream;
    mu_coin::serialize_block (stream, block_a);
    adopt (stream);
}

mu_coin::dbt::dbt (mu_coin::uint256_union const & address_a)
{
    mu_coin::byte_write_stream stream;
    address_a.serialize (stream);
    adopt (stream);
}

void mu_coin::dbt::adopt (mu_coin::byte_write_stream & stream_a)
{
    data.set_data (stream_a.data);
    data.set_size (stream_a.size);
    stream_a.data = nullptr;
}

mu_coin::dbt::dbt (mu_coin::address const & address_a, mu_coin::block_hash const & hash_a)
{
    mu_coin::byte_write_stream stream;
    stream.write (address_a.bytes);
    stream.write (hash_a.bytes);
    adopt (stream);
}

mu_coin::node::node (boost::asio::io_service & service_a, uint16_t port, mu_coin::ledger & ledger_a) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::udp::v4 (), port)),
service (service_a),
ledger (ledger_a),
keepalive_req_count (0),
keepalive_ack_count (0),
publish_req_count (0),
publish_ack_count (0),
publish_nak_count (0),
unknown_count (0),
on (true)
{
}

void mu_coin::node::receive ()
{
    socket.async_receive_from (boost::asio::buffer (buffer), remote, [this] (boost::system::error_code const & error, size_t size_a) {receive_action (error, size_a); });
}

void mu_coin::node::stop ()
{
    on = false;
    socket.close ();
}

void mu_coin::node::send_keepalive (boost::asio::ip::udp::endpoint const & endpoint_a)
{
    auto message (new mu_coin::keepalive_req);
    socket.async_send_to (message->buffers, endpoint_a, [message] (boost::system::error_code const &, size_t) {delete message;});
}

void mu_coin::node::publish_block (boost::asio::ip::udp::endpoint const & endpoint_a, std::unique_ptr <mu_coin::block> block)
{
    auto message (new mu_coin::publish_req (std::move (block)));
    mu_coin::byte_write_stream stream;
    message->serialize (stream);
    socket.async_send_to (boost::asio::buffer (stream.data, stream.size), endpoint_a, [message] (boost::system::error_code const &, size_t) {delete message;});
}

void mu_coin::node::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        if (size_a >= sizeof (uint16_t))
        {
            mu_coin::byte_read_stream type_stream (buffer.data (), size_a);
            uint16_t network_type;
            type_stream.read (network_type);
            mu_coin::type type (static_cast <mu_coin::type> (ntohs (network_type)));
            switch (type)
            {
                case mu_coin::type::keepalive_req:
                {
                    ++keepalive_req_count;
                    auto sender (remote);
                    receive ();
                    auto message (new mu_coin::keepalive_ack);
                    socket.async_send_to (message->buffers, sender, [message] (boost::system::error_code const & error, size_t size_a) {delete message;});
                    break;
                }
                case mu_coin::type::keepalive_ack:
                {
                    ++keepalive_ack_count;
                    receive ();
                    break;
                }
                case mu_coin::type::publish_req:
                {
                    ++publish_req_count;
                    auto sender (remote);
                    auto incoming (new mu_coin::publish_req);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    receive ();
                    if (!error)
                    {
                        auto process_error (ledger.process (*incoming->block));
                        if (!process_error)
                        {
                            auto outgoing (new (mu_coin::publish_ack));
                            socket.async_send_to (outgoing->buffers, sender, [outgoing] (boost::system::error_code const & error, size_t size_a) {delete outgoing;});
                        }
                        else
                        {
                            auto outgoing (new (mu_coin::publish_nak));
                            socket.async_send_to (outgoing->buffers, sender, [outgoing] (boost::system::error_code const & error, size_t size_a) {delete outgoing;});
                        }
                    }
                    break;
                }
                case mu_coin::type::publish_ack:
                {
                    ++publish_ack_count;
                    break;
                }
                case mu_coin::type::publish_nak:
                {
                    ++publish_nak_count;
                    break;
                }
                default:
                {
                    ++unknown_count;
                    receive ();
                    break;
                }
            }
        }
    }
}

mu_coin::keepalive_req::keepalive_req () :
type (htons (static_cast <uint16_t> (mu_coin::type::keepalive_req)))
{
    buffers [0] = boost::asio::const_buffer (&type, sizeof (type));
}

mu_coin::keepalive_ack::keepalive_ack () :
type (htons (static_cast <uint16_t> (mu_coin::type::keepalive_ack)))
{
    buffers [0] = boost::asio::const_buffer (&type, sizeof (type));
}

mu_coin::publish_req::publish_req (std::unique_ptr <mu_coin::block> block_a) :
type (htons (static_cast <uint16_t> (mu_coin::type::publish_req))),
block (std::move (block_a))
{
}

mu_coin::publish_req::publish_req () :
type (htons (static_cast <uint16_t> (mu_coin::type::publish_req)))
{
}

bool mu_coin::publish_req::deserialize (mu_coin::byte_read_stream & stream)
{
    auto result (false);
    result = stream.read (type);
    assert (!result);
    block = mu_coin::deserialize_block (stream);
    result = block == nullptr;
    return result;
}

void mu_coin::publish_req::serialize (mu_coin::byte_write_stream & stream)
{
    stream.write (type);
    mu_coin::serialize_block (stream, *block);
}

mu_coin::message::~message ()
{
}

mu_coin::publish_ack::publish_ack () :
type (htons (static_cast <uint16_t> (mu_coin::type::publish_ack)))
{
    buffers [0] = boost::asio::mutable_buffer (&type, sizeof (type));
}

mu_coin::publish_nak::publish_nak () :
type (htons (static_cast <uint16_t> (mu_coin::type::publish_nak)))
{
    buffers [0] = boost::asio::mutable_buffer (&type, sizeof (type));
}

mu_coin::wallet_temp_t mu_coin::wallet_temp;


mu_coin::dbt::dbt (mu_coin::private_key const & prv, mu_coin::secret_key const & key, mu_coin::uint128_union const & iv)
{
    mu_coin::uint256_union encrypted (prv, key, iv);
    mu_coin::byte_write_stream stream;
    stream.write (encrypted.bytes);
    adopt (stream);
}

mu_coin::wallet::wallet (boost::filesystem::path const & path_a) :
handle (nullptr, 0)
{
    handle.open (nullptr, path_a.native().c_str (), nullptr, DB_HASH, DB_CREATE | DB_EXCL, 0);
}

mu_coin::wallet::wallet (mu_coin::wallet_temp_t const &) :
wallet (boost::filesystem::unique_path ())
{
}

void mu_coin::wallet::insert (mu_coin::public_key const & pub, mu_coin::private_key const & prv, mu_coin::uint256_union const & key_a)
{
    dbt key (pub);
    dbt value (prv, key_a, pub.owords [0]);
    auto error (handle.put (0, &key.data, &value.data, 0));
    assert (error == 0);
}

void mu_coin::wallet::insert (mu_coin::private_key const & prv, mu_coin::uint256_union const & key)
{
    mu_coin::public_key pub;
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
    insert (pub, prv, key);
}

bool mu_coin::wallet::fetch (mu_coin::public_key const & pub, mu_coin::secret_key const & key_a, mu_coin::private_key & prv)
{
    dbt key (pub);
    dbt value;
    auto result (false);
    auto error (handle.get (0, &key.data, &value.data, 0));
    if (error == 0)
    {
        value.key (key_a, pub.owords [0], prv);
        if (!result)
        {
            mu_coin::public_key compare;
            ed25519_publickey (prv.bytes.data (), compare.bytes.data ());
            if (!(pub == compare))
            {
                result = true;
            }
        }
    }
    else
    {
        result = true;
    }
    return result;
}

void mu_coin::dbt::key (mu_coin::uint256_union const & key_a, mu_coin::uint128_union const & iv, mu_coin::private_key & prv)
{
    mu_coin::uint256_union encrypted;
    mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t *> (data.get_data ()), data.get_size ());
    auto result (stream.read (encrypted.bytes));
    assert (!result);
    prv = encrypted.prv (key_a, iv);
}

mu_coin::key_iterator::key_iterator (Dbc * cursor_a) :
cursor (cursor_a)
{
}

mu_coin::key_iterator & mu_coin::key_iterator::operator ++ ()
{
    auto result (cursor->get (&key.data, &data.data, DB_NEXT));
    if (result == DB_NOTFOUND)
    {
        cursor->close ();
        cursor = nullptr;
    }
    return *this;
}

mu_coin::public_key mu_coin::key_iterator::operator * ()
{
    return key.uint256 ();
}

mu_coin::key_iterator mu_coin::wallet::begin ()
{
    Dbc * cursor;
    handle.cursor (0, &cursor, 0);
    mu_coin::key_iterator result (cursor);
    ++result;
    return result;
}

mu_coin::key_iterator mu_coin::wallet::end ()
{
    return mu_coin::key_iterator (nullptr);
}

bool mu_coin::key_iterator::operator == (mu_coin::key_iterator const & other_a) const
{
    return cursor == other_a.cursor;
}

bool mu_coin::key_iterator::operator != (mu_coin::key_iterator const & other_a) const
{
    return !(*this == other_a);
}

std::unique_ptr <mu_coin::send_block> mu_coin::wallet::send (mu_coin::ledger & ledger_a, mu_coin::public_key const & destination, mu_coin::uint256_t const & coins, mu_coin::uint256_union const & key)
{
    bool result (false);
    mu_coin::uint256_t amount;
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    block->outputs.push_back (mu_coin::send_output (destination, coins));
    std::vector <mu_coin::public_key> accounts;
    for (auto i (begin ()), j (end ()); i != j && !result && amount < coins + block->fee (); ++i)
    {
        auto account (*i);
        auto balance (ledger_a.balance (account));
        if (!balance.is_zero ())
        {
            block->inputs.push_back (mu_coin::send_input ());
            accounts.push_back (account);
            auto & input (block->inputs.back ());
            mu_coin::block_hash latest;
            assert (!ledger_a.store.latest_get (account, latest));
            if (amount + balance > coins + block->fee ())
            {
                auto partial (coins + block->fee () - amount);
                assert (partial < balance);
                input.coins = balance - partial;
                input.previous = account ^ latest;
                amount += partial;
            }
            else
            {
                input.coins = mu_coin::uint256_t (0);
                input.previous = account ^ latest;
                amount += balance;
            }
        }
    }
    assert (amount <= coins + block->fee ());
    if (!result && amount == coins + block->fee ())
    {
        auto message (block->hash ());
        auto k (accounts.begin ());
        for (auto i (block->inputs.begin ()), j (block->inputs.end ()); i != j && !result; ++i, ++k)
        {
            assert (k != accounts.end ());
            auto & account (*k);
            mu_coin::private_key prv;
            result = fetch (account, key, prv);
            assert (!result);
            block->signatures.push_back (mu_coin::uint512_union ());
            sign_message (prv, account, message, block->signatures.back ());
        }
    }
    else
    {
        result = true;
    }
    if (result)
    {
        block.release ();
    }
    return block;
}

mu_coin::client::client (boost::asio::io_service & service_a, uint16_t port_a, boost::filesystem::path const & wallet_path_a, boost::filesystem::path const & block_store_path_a, mu_coin::processor_service & processor_a) :
processor (processor_a),
store (block_store_path_a),
ledger (store),
wallet (wallet_path_a),
network (service_a, port_a, ledger)
{
}

void mu_coin::byte_read_stream::abandon ()
{
    data = nullptr;
    end = nullptr;
}

mu_coin::uint256_union mu_coin::uint256_union::operator ^ (mu_coin::uint256_union const & other_a) const
{
    mu_coin::uint256_union result;
    auto k (other_a.qwords.begin ());
    auto l (result.qwords.begin ());
    for (auto i (qwords.begin ()), j (qwords.end ()); i != j; ++i, ++k, ++l)
    {
        *l = *i ^ *k;
    }
    return result;
}

mu_coin::uint256_union::uint256_union (uint64_t value)
{
    *this = mu_coin::uint256_t (value);
}

mu_coin::uint256_union mu_coin::dbt::uint256 () const
{
    assert (data.get_size () == 32);
    mu_coin::uint256_union result;
    mu_coin::byte_read_stream stream (reinterpret_cast <uint8_t const *> (data.get_data ()), data.get_size ());
    stream.read (result);
    stream.abandon ();
    return result;
}

mu_coin::dbt::dbt (bool y_component)
{
    uint8_t y_byte (y_component);
    mu_coin::byte_write_stream stream;
    stream.write (y_byte);
    adopt (stream);
}

void mu_coin::processor_service::run ()
{
    std::unique_lock <std::mutex> lock (mutex);
    while (!done)
    {
        std::function <void ()> operation;
        if (!operations.empty ())
        {
            operation = operations.front ();
            operations.pop ();
        }
        if (operation)
        {
            lock.unlock ();
            operation ();
            lock.lock ();
        }
        else
        {
            condition.wait (lock);
        }
    }
}

void mu_coin::processor_service::add (std::function <void ()> const & operation)
{
    std::lock_guard <std::mutex> lock (mutex);
    operations.push (operation);
    condition.notify_all ();
}

mu_coin::processor_service::processor_service () :
done (false)
{
}

void mu_coin::processor_service::stop ()
{
    std::lock_guard <std::mutex> lock (mutex);
    done = true;
    condition.notify_all ();
}

mu_coin::processor::processor (mu_coin::processor_service & service_a) :
service (service_a)
{
}