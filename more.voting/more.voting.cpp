#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>

using namespace eosio;
using namespace std;

typedef uint8_t vcount;

struct proposal_content {

    proposal_content() {}

    name pname;
    string description;

    friend bool operator == ( const proposal_content& a, const proposal_content& b ) {
        return a.pname == b.pname;
    }

    EOSLIB_SERIALIZE( proposal_content, (pname)(description))
};

struct proposal : public proposal_content {

    proposal() {}

    proposal( account_name aname, proposal_content content ) {
        proposer = aname;
        pname = content.pname;
        description = content.description;
    }

    account_name proposer;
    vcount votes = 0;

    EOSLIB_SERIALIZE_DERIVED( proposal, proposal_content, (proposer)(votes))
};

struct proposal_finder {
    name pname;
    proposal_finder(name n) : pname(n) {}
    bool operator() (const proposal& p) { return p.pname == pname; }
};

bool proposal_compare( const proposal& p1, const proposal& p2 ) {
    return p1.votes > p2.votes;
}

class voting : public contract {
    using contract::contract;
public:
    voting( account_name self ) :
            contract( self ){}

    // @abi action
    void create( account_name creator, name vname, time expiration, vector<proposal_content> proposals ) {
        require_auth( creator );

        eosio_assert( expiration > now(), "expiration cannot be earlier than current" );

        vrecords record_table(_self, creator);
        eosio_assert( record_table.find( vname ) == record_table.end(), "voting with the same name exists" );

        vector<proposal> i_proposals;

        for (auto prop_itr = proposals.begin(); prop_itr != proposals.end() ; ++prop_itr) {
            proposal i_proposal = proposal(creator, *prop_itr);
            i_proposals.push_back(i_proposal);
        }

        record_table.emplace(creator, [&]( auto& row ) {
            row.vname = vname;
            row.expiration = expiration;
            row.proposals = move(i_proposals);
        });
    }

    // @abi action
    void propose( account_name proposer, account_name creator, name vname, proposal_content content ) {
        require_auth( proposer );

        vrecords record_table(_self, creator);
        auto record_itr = record_table.find( vname );
        eosio_assert( record_itr != record_table.end(), "voting with the name not found" );

        eosio_assert( record_itr->expiration < now(), "voting has expired" );

        proposal i_proposal = proposal(proposer, content);

        auto prop_itr = std::find( record_itr->proposals.begin(), record_itr->proposals.end(), i_proposal );
        eosio_assert( prop_itr == record_itr->proposals.end(), "proposal with the same name exists" );

        record_table.modify( record_itr, proposer, [&]( auto& row ) {
            row.proposals.push_back( i_proposal );
        });

    }

    // @abi action
    void unpropose( account_name creator, name vname, name pname ) {

        vrecords record_table(_self, creator);
        auto record_itr = record_table.find( vname );
        eosio_assert( record_itr != record_table.end(), "voting with the name not found" );

        eosio_assert( record_itr->expiration < now(), "voting has expired" );

        auto prop_itr = std::find_if( record_itr->proposals.begin(), record_itr->proposals.end(), proposal_finder(pname) );
        eosio_assert( prop_itr != record_itr->proposals.end(), "proposal with the name not found" );

        account_name proposer = prop_itr->proposer;

        require_auth(proposer);

        record_table.modify( record_itr, proposer, [&]( auto& row ) {
            row.proposals.erase( prop_itr );
        });

    }

    // @abi action
    void vote( account_name voter, account_name creator, name vname, name pname ) {
        require_auth(voter);

        vrecords record_table(_self, creator);
        auto record_itr = record_table.find( vname );
        eosio_assert( record_itr != record_table.end(), "voting with the name not found" );

        eosio_assert( record_itr->expiration > now(), "voting has expired" );

        auto voters_itr = std::find( record_itr->voters.begin(), record_itr->voters.end(), voter );
        eosio_assert( voters_itr == record_itr->voters.end(), "the voter have already voted" );

        auto prop_itr = std::find_if( record_itr->proposals.begin(), record_itr->proposals.end(), proposal_finder(pname) );
        eosio_assert( prop_itr != record_itr->proposals.end(), "proposal with the name not found" );

        record_table.modify( record_itr, voter, [&]( auto& row ) {
            row.voters.push_back( voter );
            auto p_itr = std::find_if( row.proposals.begin(), row.proposals.end(), proposal_finder(pname) );
            p_itr->votes++;
        });

    }

    // @abi reveal
    void reveal( account_name creator, name vname ) {
        require_auth(creator);

        vrecords record_table(_self, creator);
        auto record_itr = record_table.find( vname );
        eosio_assert( record_itr != record_table.end(), "voting with the name not found" );

        record_table.modify( record_itr, creator, [&]( auto& row ) {
            std::sort( row.proposals.begin(), row.proposals.end(), proposal_compare );
        });

    }

    // @abi action
    void cancel( account_name creator, name vname ) {
        require_auth(creator);

        vrecords record_table(_self, creator);
        auto iterator = record_table.find( vname );
        eosio_assert( iterator != record_table.end(), "voting with the name not found" );

        record_table.erase( iterator );
    }

private:
    // @abi table
    struct vrecord {
        name vname;
        time expiration;
        vector<proposal> proposals;
        vector<account_name> voters;

        name primary_key() const { return vname; }
    };

    typedef multi_index<N(vrecord), vrecord> vrecords;
};

EOSIO_ABI( voting, (create)(propose)(unpropose)(vote)(reveal)(cancel) )
